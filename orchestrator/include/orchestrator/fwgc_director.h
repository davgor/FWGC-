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
 * fwgc_api_version() == FWGC_API_VERSION before its first call. */
#define FWGC_API_VERSION 1

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
 * `target` (NULL for none) is a place id, person uid or quest id for the acts that need one.
 * The answer is a packet: as JSON in *packet_json, and in the contract's text form (what the
 * communicator reads) in *packet_text. Either may be NULL. */
FWGC_API fwgc_status fwgc_respond(fwgc_director* director, const char* speaker, const char* act, const char* target,
                                  char** packet_json, char** packet_text) FWGC_NOEXCEPT;
/* The player finished (`completed` 1) or failed (0) work they took. */
FWGC_API fwgc_status fwgc_resolve(fwgc_director* director, const char* quest_id, int32_t completed) FWGC_NOEXCEPT;
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

/* ---- what the game asks ------------------------------------------------------------------- */

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
