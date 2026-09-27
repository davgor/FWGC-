// The director: one world, changed only through the generator's logged verbs, and the
// dialogue acts a player makes, answered as knowledge-scoped fact packets.
//
// Deterministic by construction. There is no clock, no randomness and no I/O in here, and the
// world changes in exactly two places: advance() calls fwg::advance_world_time, and accepting
// or abandoning a quest calls fwg::world_quest_request. Both append to the world's
// history.operations. The same starting world and the same inputs give the same world and the
// same packets; replay() is how a test proves it and how a save is restored.
//
// It also keeps the roster's days: when each post sleeps, walks to work, works and walks home,
// and how a hold -- the game keeping someone where they stand -- delays what comes next. A day
// is worked out from the person and the clock when asked, and a hold changes nothing else.
//
// This header names the generator, so only what depends on the generator may include it. The
// communicator includes orchestrator/contract.hpp alone.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fwg/api/world.hpp"
#include "orchestrator/contract.hpp"

namespace orchestrator {

// A caller's mistake: a uid the world does not name, an act without the target it needs, a
// span of time that is not a span. What the WORLD refuses is an answer, not an error: a
// packet whose outcome is Refused.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The inputs a session applies, in order: what replay() re-applies.
struct Advance {
    double days = 0.0;
};
struct Say {
    std::string speaker;
    DialogueAct act;
};
// What the game reports the player did with work they took: finished it, or failed it.
struct Resolve {
    std::string quest_id;
    bool completed = true;
};
// The game kept a person where they stood, from the clock on, for this long.
struct Hold {
    std::string person;
    double minutes = 0.0;
};
// The game reports something frightening a person, at a strength from 1 to 3.
struct Fright {
    std::string person;
    int level = 1;
};
// The game reports people together, near enough to see and hear each other.
struct Gather {
    std::vector<std::string> people;
};
// The game reports people who overheard the exchange just made.
struct Overhear {
    std::vector<std::string> people;
};
// The game's cron job: news reaches the people named, or everyone when none is, as far as it has
// travelled.
struct Share {
    std::vector<std::string> people;
};
// The game reports how a threat at a place ended: "beaten", "lost" or "passed", and by whom
// ("player", a person uid, or empty for no one).
struct Report {
    std::string site;
    std::string outcome;
    std::string by;
};
using Input = std::variant<Advance, Say, Resolve, Hold, Fright, Gather, Overhear, Share, Report>;

// What someone who overheard the player made of it.
enum class Overheard { stays_out, calmed, joins };
std::string_view to_string(Overheard reaction);

// A bystander and their reaction; `packet` is the reply of one who joins in.
struct Bystander {
    std::string person;
    Overheard reaction = Overheard::stays_out;
    std::optional<FactPacket> packet;
};

// How a person feels, on the fear ladder: "calm", "uneasy", "afraid", "panicked". The game shows
// `now` as their expression, and `resting` when nothing has frightened them.
struct Feeling {
    std::string now;
    std::string resting;
};

// ---- the brain database -------------------------------------------------------------------------
//
// What people have heard and remember, kept in an in-memory SQLite database. The game reads it
// with SQL (Director::query); only the director writes it, through the inputs it records, so
// replay rebuilds it. README.md, "The brain database", has the schema.

// One value a query hands back: NULL, an integer, a real, or text.
using Cell = std::variant<std::monostate, std::int64_t, double, std::string>;

struct QueryResult {
    std::vector<std::string> columns;
    std::vector<std::vector<Cell>> rows;
};

// The database a director keeps its brains in. A value: copying a director copies its database.
class Brains {
public:
    struct Handle;  // the connection, in src/brains.hpp

    Brains();
    ~Brains();
    Brains(const Brains& other);
    Brains& operator=(const Brains& other);
    Brains(Brains&& other) noexcept;
    Brains& operator=(Brains&& other) noexcept;

    Handle& handle() const { return *handle_; }

private:
    std::unique_ptr<Handle> handle_;
};

// Something that happened that people can come to know of. The director makes each where it
// sees one happen -- work offered, taken, finished, failed, given up or lapsing on the board; a
// hidden face the player brings out; frights at a place -- or where the game reports one: how a
// threat ended. Its state moves on (the attack becomes "beaten, by the player"), and whoever hears
// the newer state knows the event by it.
struct Event {
    std::string id;         // "event-1", "event-2", ... in the order made
    std::string kind;       // "work", "exposed", "scare"
    double day = 0.0;       // when it began
    std::string place;      // the place it is about, by the id a packet names it by; empty for none
    std::string place_name; // as a speaker says it
    std::optional<int> node;        // where word of it starts: the giver's place, or where it happened
    std::optional<int> about_node;  // the terrain node of the place it is about
    std::string person;     // whom it concerns: the giver, the one exposed
    std::string person_name;  // as a stranger knew them
    std::string quest_id;   // work only
    std::string verb;       // work: what the giver asks
    std::string purpose;    // work: why they say they ask it, never what it is really for
    int difficulty = 0;     // work: 1 to 5
    std::string was;        // exposed: the role the face the player knew claimed
    std::string revealed;   // exposed: the role of the face now out
    bool player = false;    // the player has had a part in it
    std::string state;      // its latest state
    double state_day = 0.0; // since when
    std::string actor;      // who brought the latest state about: "player", a person uid, or empty
};

// Someone who heard an event's state, as share() reports it.
struct Learned {
    std::string person;
    std::string event;
    std::string state;
};

// Who knows an event: at the state they know it by, and who would hear of it at the next share.
struct Related {
    std::vector<std::string> knows;
    std::vector<std::string> would_learn;
};

// ---- a roster person's day --------------------------------------------------------------------

// What someone with a day is doing. `held` is the game keeping them where they stand.
enum class Doing { asleep, at_home, walking, working, held };
std::string_view to_string(Doing doing);

// One stretch of a day, [from_day, to_day) in world days. Where the person is, is told along the
// way from their home to their workplace: 0 at home, 1 at work, and between on a walk. Someone
// the plan built no bed for lives at their post, and is at 1 throughout.
struct Stint {
    Doing doing = Doing::at_home;
    double from_day = 0.0;
    double to_day = 0.0;
    double from_along = 0.0;
    double to_along = 0.0;  // the same as from_along but on a walk
};

// What a hold made of a person's day: their stints from the hold's start to the end of the
// stretch they reached after it, from which the day runs as planned again.
struct Detour {
    double from_day = 0.0;
    double to_day = 0.0;
    std::vector<Stint> stints;  // tiling [from_day, to_day)
};

// One roster post with a day and the stretch they are in: what positions() lists, with no
// strings, so a whole roster costs one pass.
struct Position {
    std::uint32_t person = 0;  // index into world().npcs.people: uid, site, home and workplace plots
    Stint stint;               // the stretch under way at the clock
};

// Where a roster person is at one moment.
struct Whereabouts {
    std::string person;
    std::string site;            // the roster's site uid
    std::string workplace_plot;  // plot ids in that site's plan
    std::string home_plot;       // empty for someone who lives at their post
    double day = 0.0;            // the moment
    double along = 0.0;          // 0 at home, 1 at work
    Stint stint;                 // the stretch under way
};

class Director {
public:
    explicit Director(fwg::World world);

    const fwg::World& world() const { return world_; }
    const std::vector<Input>& inputs() const { return inputs_; }

    // Time passes: `days` of it, through fwg::advance_world_time. Throws Error for a span that
    // is negative, not finite, or an age or more long (an age turning is a separate decision,
    // not something a conversation's clock does), and when the world refuses the span. Every
    // conversation under way ends: time passing is walking away.
    void advance(double days);

    // The game reports taken work finished (`completed`) or failed, through
    // fwg::advance_world_time's resolutions with no time passing. The giver remembers it.
    // Throws Error for work the world does not hold open, or holds but not taken.
    void resolve(const std::string& quest_id, bool completed);

    // One act from the player to `speaker`, a person uid. Accepting or abandoning a quest
    // changes the world; nothing else here does, though every act changes what the speaker
    // remembers (the Memory fact). Throws Error for a uid the world does not name and for an
    // act missing the target it needs.
    FactPacket respond(const std::string& speaker, const DialogueAct& act);

    // What one speaker and the player have between them. Folded from the inputs, so replay
    // rebuilds it and a save is the inputs alone.
    struct Acquaintance {
        int conversations = 0;  // begun, the one under way included
        bool talking = false;   // under way: none after a farewell, time passing, or talk with another
        double last_day = 0.0;  // the world day of the last act to this speaker
        std::string since;      // how long before this conversation the last one was, roughly
        int turn = 0;           // acts in this conversation
        int disposition = 0;    // -3 hostile .. 3 warm
        bool thanked = false;   // in this conversation: thanks count once
        bool calmed = false;    // in this conversation: being eased by the player counts once
        double eased_day = -1.0;  // the world day the player last eased them; below zero: never
        std::vector<std::pair<ActKind, std::string>> said;  // this conversation's acts
        std::map<ActKind, int> heard;  // every act the player made to this speaker, counted, in any conversation
        std::vector<int> grievances;   // what each insult, and each of the speaker's work given up, cost the
                                       // player's regard, oldest first: an apology gives back the latest
        int forgiven = 0;              // the grievances apologies have eased
        std::string asked_now, asked_before;  // the last place or person asked about, by name
        std::string work;       // the player's latest dealing with this speaker's own work
        bool exposed = false;          // the player made a hidden face come out
        bool finished_covert = false;  // finished work this speaker gave under a false purpose
        bool heard_truth = false;      // someone who knows them told the player what they are
        bool seen_unleashed = false;   // met a beast unleashed
        bool seen_second = false;      // met a fractured heart's second face
    };
    // Nullptr for someone the session holds nothing of: never spoken to, never dealt with, never
    // told the truth of.
    const Acquaintance* acquaintance(const std::string& speaker) const;

    // ---- days ------------------------------------------------------------------------------
    //
    // Every living roster post has a day, by the hours of the building they work in (sleep, the
    // walk to work, work, the walk home, the evening at home), the walk as long as their commute.
    // Heroes, dreads, villains and the dead have none: they stand where the world says.

    // The game kept `person` where they stood for `minutes` from the clock on: a conversation, a
    // road blocked, anything. Their day makes room for it. After the hold they make for where
    // their day has them; a stretch they cannot reach before its end is skipped for the next,
    // and each stretch they reach still ends when it was to. So a walk to work held 20 minutes
    // arrives 20 minutes late and the shop still shuts on time, open 20 minutes less; a hold at
    // work within its hours costs nothing, and one past closing comes out of the evening. By
    // the end of the stretch they reach, the day runs as planned again. Changes neither the
    // world nor what anyone remembers. Throws Error for minutes that are not more than zero and
    // at most a day, and for a uid the world does not name. Holding someone with no day is
    // recorded and changes nothing.
    void hold(const std::string& person, double minutes);

    // Where a roster post is now, and doing what; nullopt for anyone with no day.
    std::optional<Whereabouts> whereabouts(const std::string& person) const;

    // The stretches of a roster post's day that meet the calendar day holding `day` (sunrise to
    // sunrise), with every hold so far; empty for anyone with no day.
    std::vector<Stint> day_of(const std::string& person, double day) const;

    // Everyone with a day at one site, now, in roster order; empty for a site the roster does
    // not name.
    std::vector<Whereabouts> whereabouts_at(const std::string& site) const;

    // Every roster post with a day and the stretch they are in now, in roster order: one pass
    // over the roster, copying no strings, for asking after everyone at once. On a walk, where
    // they are at any moment of it is its along in proportion to the time. A stretch changes
    // only when it ends or when someone is held, so a caller can keep them until then.
    std::vector<Position> positions() const;

    // ---- feelings ----------------------------------------------------------------------------
    //
    // Everyone rests calm, or uneasy where a nest of danger 4 or more lies within a kilometre of
    // where they stand. Fear rises with what the game reports, spreads through people together,
    // and falls a step toward rest for every half hour nothing new frightens them; the player can
    // talk someone down (the act calm). The Memory fact of every packet says how the speaker feels.

    // The game reports something frightening `person`, a threat near them, at `level`: 1
    // uneasy, 2 afraid, 3 panicked. Their fear rises to it when it is lower, and it counts as
    // still there for a quarter of an hour: calming words do not ease them while it is. Throws
    // Error for a level outside 1..3 and for a uid the world does not name.
    void fright(const std::string& person, int level);

    // The game reports `people` together, near enough to see and hear each other, and fear
    // spreads: each rises to one step below the most afraid of the others, and to what two of the
    // others share. So one panicking villager leaves the rest afraid, and two leave them panicking.
    // Nobody's fear falls for it. Throws Error for a uid the world does not name.
    void gather(const std::vector<std::string>& people);

    // How `person` feels now. Throws Error for a uid the world does not name.
    Feeling feeling(const std::string& person) const;

    // ---- overhearing -------------------------------------------------------------------------
    //
    // The game reports `people` who overheard the exchange just made (the last respond()), near
    // enough to hear the player and the reply. Each makes of it, in order:
    // - calmed: the player's calming words eased the one spoken to, and the bystander is afraid,
    //   with nothing still frightening them and no hostility to the player: eased a step too;
    // - joins: the one spoken to could not answer, and the bystander can -- a place they did not
    //   know that the bystander knows, a person the same, work when they had none and the bystander
    //   has some -- and is neither panicking nor hostile to the player. They walk over and answer:
    //   `packet` is their reply, its Memory fact `joined`, and a conversation with them begins. At
    //   most one joins, the first who can;
    // - stays_out: everything else, the one spoken to among them.
    // Throws Error when nothing has been said yet, and for a uid the world does not name.
    std::vector<Bystander> overhear(const std::vector<std::string>& people);

    // ---- events, and what people have heard -----------------------------------------------------
    //
    // Whoever an event happened to knows it at once: a giver their own work, the one exposed, the
    // frightened. Everyone else hears of it through share(), the game's cron job, by the rules in
    // README.md ("What people hear"): those standing where word of it starts, those tied to whom
    // it concerns, and those within its reach once word has had time to travel. Someone who hears
    // that the player finished work where they live, or failed it, takes the player better or
    // worse for it, once.

    // Every event, in the order made.
    std::vector<Event> events() const;

    // News reaches `people`, or everyone the world names when none are named: each hears the
    // latest state of every event that has reached them and they do not know it by. Returns who
    // heard what, in the order of events and then people. Throws Error for a uid the world does
    // not name.
    std::vector<Learned> share(const std::vector<std::string>& people);

    // Who knows `event`, and who the next share would tell of it, changing nothing. Throws Error
    // for an event there is none of.
    Related related(const std::string& event) const;

    // The game reports how a threat at `site` ended: `outcome` "beaten", "lost" or "passed", `by`
    // "player", a person uid, or empty. `site` is a place id or a roster site's uid. It moves the
    // scare there (frights within the last day), or makes one. Throws Error for an outcome outside
    // those three, a place the world does not name, or a `by` it does not.
    void report_event(const std::string& site, const std::string& outcome, const std::string& by);

    // The game's read of the brain database: one SQL statement that only reads, its rows back.
    // Throws Error for anything else -- a write, a second statement, a pragma, a file attached --
    // and for a statement that runs too long or answers too many rows.
    QueryResult query(std::string_view sql) const;

private:
    void end_conversation(const std::string& speaker);

    fwg::World world_;
    std::vector<Input> inputs_;
    std::map<std::string, Acquaintance> acquainted_;
    std::string talking_to_;
    // What each person's holds made of their day, in the order held, none overlapping: each
    // worked out once, when held, so a day asked of later reads the same.
    std::map<std::string, std::vector<Detour>> detours_;
    // Fear on the ladder, 0 calm to 3 panicked, as of when it last rose or was eased; what
    // frightens someone is still there until a quarter of an hour after the game last said so.
    struct Fear {
        int level = 0;
        double since_day = 0.0;
        double threat_day = -1.0;  // below zero: never reported
    };
    std::map<std::string, Fear> fears_;
    // How afraid `uid` is at `now`, resting at `resting`.
    int fear_now(const std::string& uid, int resting, double now) const;
    // The last exchange, for those who overheard it: who was spoken to, what the player did, what
    // came of it, and whether the answer held nothing (a place or person unknown, no work).
    struct Exchange {
        std::string speaker;
        DialogueAct act;
        Outcome outcome = Outcome::NotUnderstood;
        bool nothing = false;
    };
    std::optional<Exchange> last_;
    // respond(), for the player or for a bystander who joins (`joined`: not an input of its own,
    // since the Overhear that made it is).
    FactPacket reply(const std::string& speaker, const DialogueAct& act, bool joined);

    // ---- the brain database ----------------------------------------------------------------------
    Brains brains_;
    int events_made_ = 0;
    // Bumped when the world changes, and when anyone's standing with the player may have: the
    // database's copies of the world's people, places and work, and of every acquaintance, are
    // written again before a query reads them only when these moved.
    std::uint64_t world_changes_ = 0;
    std::uint64_t acquaintance_changes_ = 0;
    mutable std::uint64_t mirrored_world_ = ~std::uint64_t{0};
    mutable std::uint64_t mirrored_acquaintances_ = ~std::uint64_t{0};
    void mirror() const;

    // Everyone the world names, where they stand, and whom each hero is tied to: what share reads.
    // Worked out again only when the world has changed.
    struct Someone {
        std::string name, kind, role, site;
        std::optional<int> node;
        bool present = true;
    };
    struct Population {
        std::map<std::string, Someone> people;
        std::map<int, std::vector<std::string>> at_node;
        std::map<std::string, std::set<std::string>> ties;
    };
    mutable std::optional<Population> population_;
    mutable std::uint64_t population_of_ = ~std::uint64_t{0};
    const Population& population() const;

    // The work on the board, made into events and moved on as the board moves: after anything
    // that changes the world.
    void note_board();
    std::string make_event(const Event& event);
    void move_event(const std::string& id, const std::string& state, double day, const std::string& actor);
    // `person` knows `event` at `state`, learned on `day`, `how` "there" or "word".
    void learn(const std::string& person, const std::string& event, const std::string& state, double day, const std::string& how);
    // Who the rules say word of an event has reached by now.
    std::vector<std::string> reached(const Event& event, double today) const;
    // What `speaker` has heard, as News facts, newest first, at most `most`: with `about` "player",
    // the events whose state they know the player brought about; with a person uid, the events
    // about that person; with `about` empty, the events at `node`.
    std::vector<Fact> news(const std::string& speaker, const std::string& about, std::optional<int> node, std::size_t most) const;
};

// A session again: `start` with every input re-applied, in order.
Director replay(fwg::World start, const std::vector<Input>& inputs);

}  // namespace orchestrator
