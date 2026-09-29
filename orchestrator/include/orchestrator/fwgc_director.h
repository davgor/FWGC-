/* The director, in C: what a game engine binds to.
 *
 * fwgc_director.dll holds the whole director -- a world, the player's conversations, quests,
 * feelings and every roster post's day -- behind a plain C interface: an opaque handle, UTF-8
 * text in and out, and plain structs for what a game reads every frame. Written in the C subset
 * and implemented in C++; no exception ever crosses it.
 *
 * - Every function returns an fwgc_status. On anything but FWGC_OK, fwgc_last_error() says why,
 *   in this thread, until this thread's next fwgc call; a success clears it.
 * - Every string handed back (char**) is allocated here and freed with fwgc_free, never with the
 *   engine's own allocator. Every string passed in must be UTF-8, or the call is refused and
 *   nothing is recorded.
 * - Use one handle from one thread at a time. A single lock inside makes calls safe from any
 *   thread, but a call waits while another runs, and fwgc_create, fwgc_restore and fwgc_advance
 *   run long: make them off the game thread.
 * - A save is the request that made the world and every input since. fwgc_restore makes the
 *   world again and replays them, so a save restores only where the world comes out the same:
 *   the same library build, and a C runtime that computes alike. Where it would not, the save is
 *   refused (FWGC_INCOMPATIBLE) rather than restored onto a different world.
 * - fwgc_world_json is the whole world, for drawing it: it holds what speakers keep back (hidden
 *   faces, what a hook is really for). Show players only what packets say.
 */
#ifndef FWGC_DIRECTOR_H
#define FWGC_DIRECTOR_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(fwgc_director_EXPORTS)
#define FWGC_API __declspec(dllexport)
#else
#define FWGC_API __declspec(dllimport)
#endif
#else
#define FWGC_API __attribute__((visibility("default")))
#endif

#if defined(__cplusplus)
#define FWGC_NOEXCEPT noexcept
extern "C" {
#else
#define FWGC_NOEXCEPT
#endif

/* The interface's version, bumped whenever a function, struct or code changes. A game checks
 * fwgc_api_version() == FWGC_API_VERSION before its first call.
 * 2: the brain database (fwgc_query), news (fwgc_share, fwgc_related, fwgc_report_event).
 * 3: short and long memory (fwgc_words, fwgc_compact, fwgc_memories); the database's schema 2.
 * 4: the act "complain" and its outcome "heard" (contract version 18).
 * 5: fwgc_account, and the Account fact it gives (contract version 20); the database's schema 3.
 * 6: fwgc_words takes the question the reply asked worth an answer (contract version 21); the
 *    database's schema 4 (exchanges.asked).
 * 7: the player's name (contract version 24): the act "introduce", its target the name the player gives,
 *    and the outcomes "introduced", "known" and "renamed"; fwgc_acquaintance's `called`, `called_how` and
 *    `called_other`; the database's schema 5 (relationships.called..., events.player_name).
 * 8: fame (contract version 25): "ask_about_person" takes "=" and the words the player named someone by,
 *    which the director looks up; an answer about a person carries their fame and known deed.
 * 9: heroes' deeds (contract version 26): fwgc_direct_heroes; News of a hero's deed; the database's
 *    schema 6 (events.against, events.against_name, events.scope).
 * 10: how a hero takes their deed (contract version 30): fwgc_reaction_brief, fwgc_react; the contract's
 *    "reactions"; the database's schema 7 (events.deed, events.reaction). */
#define FWGC_API_VERSION 10

typedef struct fwgc_director fwgc_director;

/* What a call came to. New codes are only ever appended. */
typedef int32_t fwgc_status;
#define FWGC_OK 0
#define FWGC_BAD_ARGUMENT 1     /* the caller's mistake: a NULL, text not UTF-8, an unknown act or
                                   uid, a span that is not one. Nothing was recorded. */
#define FWGC_BUFFER_TOO_SMALL 2 /* the count says how many there are; the buffer holds what fitted */
#define FWGC_REFUSED 3          /* the generator refused the request: fwgc_last_error() is its
                                   refusal document (JSON) where it has one */
#define FWGC_INCOMPATIBLE 4     /* a save that does not restore here: not a save, another version,
                                   or a world that comes out differently */
#define FWGC_POISONED 5         /* this handle failed part way through a change; only fwgc_save and
                                   fwgc_destroy still work. Restore it from its save. */
#define FWGC_OUT_OF_MEMORY 6
#define FWGC_FAILED 7           /* anything else, said in fwgc_last_error() */

/* What someone with a day is doing (fwgc_stint.doing). */
#define FWGC_ASLEEP 0
#define FWGC_AT_HOME 1
#define FWGC_WALKING 2
#define FWGC_WORKING 3
#define FWGC_HELD 4

#pragma pack(push, 8)

/* One stretch of a roster post's day, [from_day, to_day) in world days (the world's clock:
 * days since its founding, hour 0 of each day at sunrise). Where they are is told along the
 * way from their home to their workplace: 0 at home, 1 at work, between on a walk, in
 * proportion to the time. Someone housed nowhere lives at their post and is at 1 throughout. */
typedef struct fwgc_stint {
    int32_t doing;    /* FWGC_ASLEEP .. FWGC_HELD */
    int32_t reserved; /* 0 */
    double from_day;
    double to_day;
    double from_along;
    double to_along;
} fwgc_stint;

/* A roster post and the stretch they are in: what fwgc_positions lists. `person` indexes the
 * roster (fwgc_roster, and npcs.people in the world document). */
typedef struct fwgc_position {
    uint32_t person;
    int32_t reserved; /* 0 */
    fwgc_stint stint;
} fwgc_position;

/* Where a roster post is at one moment (`day`), and how far along the way. */
typedef struct fwgc_whereabouts {
    uint32_t person;
    int32_t reserved; /* 0 */
    double day;
    double along;
    fwgc_stint stint;
} fwgc_whereabouts;

#pragma pack(pop)

/* ---- versions and help -------------------------------------------------------------------- */

FWGC_API int32_t fwgc_api_version(void) FWGC_NOEXCEPT;
/* The version of the packets fwgc_respond writes (the orchestrator's contract). */
FWGC_API int32_t fwgc_contract_version(void) FWGC_NOEXCEPT;
/* Frees anything this library handed back. NULL is fine. */
FWGC_API void fwgc_free(void* memory) FWGC_NOEXCEPT;
/* Why this thread's last call failed; "" after a success. Never NULL. */
FWGC_API const char* fwgc_last_error(void) FWGC_NOEXCEPT;
/* How many threads making a world may use: 0 (the default) every hardware thread, 1 none but
 * the caller's. A world comes out the same at any count. */
FWGC_API void fwgc_set_worker_threads(uint32_t count) FWGC_NOEXCEPT;
/* The contract as JSON: every act by name with whether it needs a target and whether it
 * changes the world (a game confirms those with the player before handing them on), every
 * outcome and fact kind by name, and the activity codes. */
FWGC_API fwgc_status fwgc_contract_json(char** json) FWGC_NOEXCEPT;

/* ---- a director's life -------------------------------------------------------------------- */

/* A new world from a generator request (JSON: {"seed": 42, "overrides": {"size": 65}}, the
 * shape fwg.exe --request reads), and a director over it. *director is NULL on failure. */
FWGC_API fwgc_status fwgc_create(const char* request_json, fwgc_director** director) FWGC_NOEXCEPT;
/* A director from a save (fwgc_save): the world made again and every input replayed. */
FWGC_API fwgc_status fwgc_restore(const char* save_json, fwgc_director** director) FWGC_NOEXCEPT;
/* NULL is fine. */
FWGC_API void fwgc_destroy(fwgc_director* director) FWGC_NOEXCEPT;
/* The save: JSON, ASCII only. Works on a poisoned handle, and holds only what succeeded.
 * `size` (may be NULL) gets its length. */
FWGC_API fwgc_status fwgc_save(const fwgc_director* director, char** json, size_t* size) FWGC_NOEXCEPT;
/* The world document as it stands now: tens of megabytes. For drawing the world only. */
FWGC_API fwgc_status fwgc_world_json(const fwgc_director* director, char** json, size_t* size) FWGC_NOEXCEPT;
/* The world's clock: days since its founding; hour 0 of each day is sunrise. */
FWGC_API fwgc_status fwgc_clock(const fwgc_director* director, double* day) FWGC_NOEXCEPT;

/* ---- what the game reports: each is recorded, and replayed from a save -------------------- */

/* Time passes: `days` of it, zero or more and less than an age. Every conversation ends. */
FWGC_API fwgc_status fwgc_advance(fwgc_director* director, double days) FWGC_NOEXCEPT;
/* The player makes `act` (by name: "greet", "ask_about_place", ...) to `speaker`, a person uid;
 * `target` (NULL for none) is a place id, person uid or quest id for the acts that need one, and for
 * "introduce" the name the player gives, as they said it (1 to 60 bytes). For "ask_about_person" it may
 * instead be "=" and the words the player named someone by ("=the king", "=Aldomir"; 1 to 60 bytes), which
 * the director looks up among the people the speaker knows (contract version 25).
 * The answer is a packet: as JSON in *packet_json, and in the contract's text form (what the
 * communicator reads) in *packet_text. Either may be NULL. */
FWGC_API fwgc_status fwgc_respond(fwgc_director* director, const char* speaker, const char* act, const char* target,
                                  char** packet_json, char** packet_text) FWGC_NOEXCEPT;
/* The player finished (`completed` 1) or failed (0) work they took. */
FWGC_API fwgc_status fwgc_resolve(fwgc_director* director, const char* quest_id, int32_t completed) FWGC_NOEXCEPT;
/* How work the player finished or failed went, as the game saw it: `came`, what came (a nest id or a
 * person uid); `many`, how many ("one", "few", "many", "horde"); `cost`, what it cost those who stood
 * there ("none", "few", "many" lost); `part`, the player's ("fought", "led", "turned", "held", "hurt").
 * Any of the four may be NULL for not said. Kept on the work's event (the database's `accounts`);
 * its giver tells it, from an Account fact in their greetings and small talk while that work's end
 * is their latest dealing with the player. A later account of the same work replaces it. Refused
 * (FWGC_BAD_ARGUMENT) for work the player has not finished or failed, a nest or person the world
 * does not name, and a word off its list. */
FWGC_API fwgc_status fwgc_account(fwgc_director* director, const char* quest_id, const char* came, const char* many,
                                  const char* cost, const char* part) FWGC_NOEXCEPT;
/* The game kept `person` where they stood for `minutes` (more than 0, at most a day) from now. */
FWGC_API fwgc_status fwgc_hold(fwgc_director* director, const char* person, double minutes) FWGC_NOEXCEPT;
/* Something frightened `person`: `level` 1 uneasy, 2 afraid, 3 panicked. */
FWGC_API fwgc_status fwgc_fright(fwgc_director* director, const char* person, int32_t level) FWGC_NOEXCEPT;
/* `count` people are together, near enough to see and hear each other: fear spreads. */
FWGC_API fwgc_status fwgc_gather(fwgc_director* director, const char* const* people, size_t count) FWGC_NOEXCEPT;
/* `count` people overheard the exchange just made (the last fwgc_respond), near enough to hear
 * the player and the reply. Each makes of it "stays_out", "calmed" (eased a step, as the one
 * spoken to was) or "joins": at most one, who could answer what the one spoken to could not, and
 * walks over. As JSON in *bystanders_json: [{"person", "reaction", "packet"}], `packet` the
 * joiner's reply (as fwgc_respond writes it) or null. *packet_text is the joiner's reply in the
 * contract's text form, "" when none joins. Either may be NULL. Refused before anything is said. */
FWGC_API fwgc_status fwgc_overhear(fwgc_director* director, const char* const* people, size_t count,
                                   char** bystanders_json, char** packet_text) FWGC_NOEXCEPT;
/* The game's cron job for news: each of the `count` people named (0 for everyone the world names)
 * hears the latest state of every event word of which has reached them -- where it happened, from
 * someone they are tied to, or from within its reach once word has had time to travel -- and that
 * they do not already know it by. Someone who hears the player finished or failed work where they
 * live takes the player better or worse for it, once. Who heard what, as JSON in *json:
 * [{"person", "event", "state"}]. May be NULL. */
FWGC_API fwgc_status fwgc_share(fwgc_director* director, const char* const* people, size_t count,
                                char** json) FWGC_NOEXCEPT;
/* Heroes act (contract version 26): each of the `count` heroes named (0 for every hero) who is due acts
 * by their claim -- defends what they hold against the worst nest within 5 km of where they stand, hunts the nest they
 * are sworn to kill, assaults the ruin they would retake -- at most once a call and a week after they last
 * did; how it goes is their fame's chance against the danger, drawn from their seed and the day, so a
 * save replays it. The world records each deed; each is an event people hear of at the next fwgc_share.
 * What each did, as JSON in *json: [{"hero", "action", "outcome", "place", "against", "day", "event",
 * "text"}]. May be NULL. */
FWGC_API fwgc_status fwgc_direct_heroes(fwgc_director* director, const char* const* heroes, size_t count,
                                        char** json) FWGC_NOEXCEPT;
/* What a picker is shown to choose how `hero` takes their deed `event` (an event fwgc_direct_heroes answered;
 * contract version 30), as JSON in *json: {"hero", "event", "name", "archetype", "alignment", "code", "manner",
 * "voice", "toward_player", "line", "never": [...], "deed", "outcome", "open": [...]}: the card's public part, as
 * a packet's persona has it, never what the card keeps back. "open" names the reactions open to how the deed went.
 * Refused for an event that is none of the hero's deeds, and a deed they took already. */
FWGC_API fwgc_status fwgc_reaction_brief(const fwgc_director* director, const char* hero, const char* event,
                                         char** json) FWGC_NOEXCEPT;
/* How `hero` takes their deed `event`: `reaction` one the brief holds open ("humbled", "embittered", "resolved",
 * "vindicated", "grieving", "vengeful"), or NULL for the rules' pick by their alignment. Applied at once and
 * recorded, so a save replays it without asking again: embittered, a step toward evil, which the world records,
 * and their persona and voice follow; resolved, a better chance at their next deed; grieving, their next week sat
 * out; vengeful, their next deed goes after this one's foe; humbled and vindicated, only how they speak of it. Their
 * own News of the deed says how they took it (`took_it`). The reaction applied, as JSON in *json: {"reaction"}. May
 * be NULL. Refused as fwgc_reaction_brief is, and for a reaction not open. */
FWGC_API fwgc_status fwgc_react(fwgc_director* director, const char* hero, const char* event, const char* reaction,
                                char** json) FWGC_NOEXCEPT;
/* How a threat at `site` (a place id or a roster site's uid) ended: `outcome` "beaten", "lost" or
 * "passed"; `by` "player", a person uid, or NULL for no one. It moves the scare there (frights
 * within the last day) or makes one; people hear of it at the next fwgc_share. */
FWGC_API fwgc_status fwgc_report_event(fwgc_director* director, const char* site, const char* outcome,
                                       const char* by) FWGC_NOEXCEPT;
/* The words of the exchange just made, as the game showed them: the player's `line` and the `reply`
 * (NULL for none), each at most 4,000 bytes. Call it once a reply is shown: after fwgc_respond for
 * its reply, after fwgc_overhear for a joiner's. The log of the one who answered keeps them, and
 * the logs of those who overheard it; the director reads no meaning into them. `asked` (NULL for
 * none, contract version 21): the question in the reply the speaker would have answered, as the
 * communicator marks it, word for word as the reply says it and at most 240 bytes; a compaction keeps
 * it when the player's next line to them did not answer it, and a later greeting carries it once, to
 * be asked again. Refused before anything is said, and for a question not in the reply. */
FWGC_API fwgc_status fwgc_words(fwgc_director* director, const char* line, const char* reply, const char* asked) FWGC_NOEXCEPT;
/* `person`'s conversation log made into long memory, and cleared: the only way a log becomes long
 * memory, and nothing calls it but the game (someone despawned, a time skip, whoever
 * active_short lists). A conversation under way with them ends. `outcomes` (NULL for none) is a
 * summary of the log written elsewhere, as JSON: {"said": [{"quote": "...", "about": "..."}],
 * "last": "..."}; each quote kept must be the stranger's own words, word for word in the log, and a
 * name the log does not hold is refused. Outcomes that are not that JSON are refused
 * (FWGC_BAD_ARGUMENT) and nothing changes. What it did, as JSON in *json (may be NULL): {"kept":
 * [memories, as fwgc_memories writes them], "refused": ["why", ...], "cleared": exchanges}. */
FWGC_API fwgc_status fwgc_compact(fwgc_director* director, const char* person, const char* outcomes,
                                  char** json) FWGC_NOEXCEPT;

/* ---- what the game asks ------------------------------------------------------------------- */

/* The brain database, read with SQL: one statement that only reads (SELECT, WITH), its rows as
 * JSON in *json: {"columns": [...], "rows": [[...], ...]}, integers and reals as numbers, text as
 * strings, NULL as null. A write, a second statement, a pragma or a file attached is refused
 * (FWGC_BAD_ARGUMENT), and so is a statement that runs too long or answers more than 100,000 rows.
 * The tables: people, places, work (the world's, as it stands), relationships (each person's
 * standing with the player), events and event_states (what happened), facts (who has heard what),
 * exchanges and exchange_facts (each person's conversation log, since they were last compacted),
 * memories (their long memory), and the views news and active_short (who has a log, and how long);
 * the orchestrator's README.md describes each. Nothing here changes the director, and a save does
 * not hold the database: replaying writes it again. */
FWGC_API fwgc_status fwgc_query(const fwgc_director* director, const char* sql, char** json) FWGC_NOEXCEPT;
/* Who knows `event` (an event id, "event-12"), and who the next fwgc_share would tell of it, changing
 * nothing: {"knows": [...], "would_learn": [...]}. */
FWGC_API fwgc_status fwgc_related(const fwgc_director* director, const char* event, char** json) FWGC_NOEXCEPT;
/* Everything in `person`'s long memory, in the order kept, as JSON: [{"person", "memory",
 * "subject", "act", "about", "times", "first_day", "last_day", "words", "source", "seq"}]. */
FWGC_API fwgc_status fwgc_memories(const fwgc_director* director, const char* person, char** json) FWGC_NOEXCEPT;

/* What `speaker` and the player have between them, as JSON; the text null for someone the
 * session holds nothing of. */
FWGC_API fwgc_status fwgc_acquaintance(const fwgc_director* director, const char* speaker, char** json) FWGC_NOEXCEPT;
/* How `person` feels, as JSON: {"now": "afraid", "resting": "calm"}, on the fear ladder calm,
 * uneasy, afraid, panicked. `now` is their expression. */
FWGC_API fwgc_status fwgc_feeling(const fwgc_director* director, const char* person, char** json) FWGC_NOEXCEPT;
/* Every roster post with a day and the stretch they are in now, in roster order. Writes up to
 * `capacity` into `out` (NULL when capacity is 0), sets *count to how many there are, and
 * returns FWGC_BUFFER_TOO_SMALL when they did not all fit. A stretch changes only when it ends
 * or someone is held, so a game can keep what it was given until then. */
FWGC_API fwgc_status fwgc_positions(const fwgc_director* director, fwgc_position* out, size_t capacity,
                                    size_t* count) FWGC_NOEXCEPT;
/* Where `person` is now. *has_day is 0, and *out untouched, for anyone with no day (a hero, a
 * dread, a villain, the dead, a uid the roster does not name). */
FWGC_API fwgc_status fwgc_whereabouts_of(const fwgc_director* director, const char* person, fwgc_whereabouts* out,
                                         int32_t* has_day) FWGC_NOEXCEPT;
/* Everyone with a day at one site, now, in roster order; buffers as fwgc_positions. */
FWGC_API fwgc_status fwgc_whereabouts_at(const fwgc_director* director, const char* site, fwgc_whereabouts* out,
                                         size_t capacity, size_t* count) FWGC_NOEXCEPT;
/* The stretches of `person`'s day that meet the calendar day holding `day` (sunrise to sunrise),
 * every hold applied; none for anyone with no day. Buffers as fwgc_positions. */
FWGC_API fwgc_status fwgc_day_of(const fwgc_director* director, const char* person, double day, fwgc_stint* out,
                                 size_t capacity, size_t* count) FWGC_NOEXCEPT;
/* Every roster post with a day, as JSON, once per world: [{"index", "uid", "name", "post",
 * "site", "workplace", "workplace_plot", "home", "home_plot"}], index as in fwgc_position. */
FWGC_API fwgc_status fwgc_roster(const fwgc_director* director, char** json) FWGC_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#endif /* FWGC_DIRECTOR_H */
