// The arcs every living hero could walk, compiled from the cast and a handful of world facts.
//
// For each living person and Dread this evaluates all 51 trope entry predicates, weights the
// eligible ones by a CLOSED DETERMINISTIC EQUATION, offers the heaviest as that hero's
// resting hook, binds the offered trope's three acts to the hero's own names, and lists the
// threads each act can exit along with the one typed condition each needs.
//
// NOTHING IS PLANNED AND NOTHING IS ROLLED. No draw decides an assignment; the seed only
// staggers initiative deadlines. Completion and abandonment predicates are exported for a
// later walker, not evaluated here.
//
// The package reads ONLY the exported `heroes` block plus five world facts. It does not
// reach into the hero generator's internals, which is what lets a saved world compile the
// same web as a live one. The reference went further and kept a second literal copy of the
// 82-token feature vocabulary so the two packages shared no symbol; in one C++ library that
// isolation argument is weaker, so the second copy is kept as DATA (story-code-tables.json
// is its own document) and a test ties the two lists together rather than a comment.
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/heroes.hpp"

namespace fwg {

inline constexpr int kStoryWebVersion = 1;

// Merged into EVERY exported act. Content, not mechanism: a walker abandons an act when
// the deadline passes, when the protagonist dies or when the target is gone.
inline constexpr std::string_view kStoryImplicitAbandonment[2] = {"protagonist_dead", "target_gone"};

// Every act's completion is a `target` predicate and every abandonment a `deadline`; the
// catalogue lint refuses anything else, so the two words are constants rather than fields.
inline constexpr std::string_view kStoryCompletionKind = "target";
inline constexpr std::string_view kStoryAbandonmentKind = "deadline";

// ---------------------------------------------------------------------------
// The catalogue, generated from data/story-*.json into
// src/generated/content_story_web.cpp by tools/embed/story_web.py.
// ---------------------------------------------------------------------------

struct StoryOption {
    std::string_view id, text;
    HeroAxisPair delta;  // each axis within +/- 0.5
    // An opaque tag. The compiler NEVER interprets it; only a later walker gives it
    // meaning, so the 93 distinct tags are stored as strings and not as an enum.
    std::string_view effect;
};

struct StoryAct {
    std::string_view id, title, prompt;
    std::string_view completion_target, completion_state;
    int abandonment_days;
    std::span<const StoryOption> options;  // two to four
};

struct StoryTrope {
    std::string_view id, name;
    // Where this arc pulls the hero. `angle_degrees` turns it into a bearing.
    HeroAxisPair pull;
    // A token may carry a leading '!', which makes the fact's ABSENCE the precondition.
    // THE FIRST CONJUNCTION WINS A TIE, in two places, both with a strict less-than: 28 of
    // the 51 tropes have two or more conjunctions of equal length, so this is live.
    std::span<const HeroClause> requires_any;
    // FILE ORDER: `boosted_by` is exported in this order.
    std::span<const HeroBoost> boosts;
    std::span<const std::string_view> fits;    // archetype ids
    std::span<const std::string_view> claims;  // two tropes declare an EMPTY list on purpose
    std::span<const std::string_view> cast;    // a non-empty subset of {person, dread}
    // EXACTLY THREE, and their POSITION is the ring number. acts[0] is the resting hook.
    std::span<const StoryAct> acts;
    std::span<const std::string_view> examples;  // prose, never evaluated
};

std::span<const StoryTrope> story_tropes();
const StoryTrope* story_trope(std::string_view id);

struct StoryWeightPolicy {
    double fit_gain;    // how hard alignment agreement tilts the weight
    double fit_floor;   // an opposed spoke is damped to a quarter, never removed
    double pull_floor;  // the divisor floor, so a nearly-neutral pull cannot blow the fit up
    double fits_factor;
    double claim_factor;
    // QUOTED ONLY. A second copy of the hero generator's live pole boundary, carried for
    // the lab and the docs. NO CODE READS IT -- do not invent a consumer. A test ties it
    // to the live value so the two cannot drift apart unnoticed.
    double threshold;
    double node_intense_threshold;
    // The seasonal coverage below which a city is in famine. A famine is a year the city
    // cannot feed itself, which is NOT the structural deficit diagnostic (that marks
    // nearly every city without farms).
    double famine_coverage;
    int max_threads_per_act;
    int initiative_days;
};
const StoryWeightPolicy& story_weight_policy();

// The 28 facts a runtime event may ADD to somebody, which is exactly what a `feature`
// thread is allowed to propose.
std::span<const std::string_view> story_gainable_features();

struct StoryConstraint {
    std::string_view archetype;  // empty on the default row
    bool claim_locked;
    bool drift_locked;
    // NULL MEANS ANY VERB; an empty list would mean NO verb. Normalising the one into the
    // other silences every constrained hero, so the two are kept apart.
    bool has_allowed_verbs;
    std::span<const std::string_view> allowed_verbs;
};
// An archetype with no row falls back to the default, which is UNCONSTRAINED rather than
// locked -- and so does a hero with no archetype at all.
const StoryConstraint& story_constraints_for(std::string_view archetype);

// The second copy of the hero feature vocabulary, from this package's own document.
std::span<const std::string_view> story_hero_features();
std::span<const std::string_view> story_world_facts();
// The union of the hero features, the world facts and the derived facts. DERIVED here
// rather than carried, so there is one thing to keep in step instead of three.
std::span<const std::string_view> story_facts();
std::span<const std::string_view> story_claim_verbs();
std::span<const std::string_view> story_bond_kinds();
// The three fame bands. A hero whose tier is outside them carries no `tier:` fact.
std::span<const std::string_view> story_tiers();
// The nineteen presence situations. A situation outside this list silently costs a hero
// its `presence:` fact, which is why the list is published rather than inferred.
std::span<const std::string_view> story_situations();
// Features the archetype catalogue reserves but no well computes. A trope whose every
// conjunction needs one is documented and cannot fire, and is listed in the ledger as
// unreachable rather than hidden.
std::span<const std::string_view> story_never_computed();

// A precondition token is known when it is a listed fact or an archetype reference.
bool story_known_fact(std::string_view token);

struct StoryFallback {
    std::string_view slot;
    std::string_view text;
};
// AUTHORED PLAYER-FACING PROSE: these reach the exported act text whenever a cast slot is
// unfilled, so an absent cast member produces this English rather than an empty slot.
std::span<const StoryFallback> story_bind_fallbacks();

// {open 0, alignment 1, claim 2, feature 3}. A POSITIONAL RANK, and it is the tie-break
// that decides which conjunction a thread condition names as well as the primary key of
// the final thread sort. -1 for an unknown kind.
int story_thread_rank(std::string_view kind);

struct StoryCatalogueIdentity {
    int schema_version;
    int tropes, constraints, weights;  // the three `revision` integers
    std::string_view sha256;
};
StoryCatalogueIdentity story_catalogue_identity();

std::string_view story_web_method();
std::string_view story_web_limits();

void lint_story_catalogue();

// ---------------------------------------------------------------------------
// What the world has to say for itself. Five facts and a translation table; every other
// world read in this package goes through the `heroes` block.
// ---------------------------------------------------------------------------

// Settlement `id` -> `uid`. The only reason it exists: `humans.cores` and
// `seasonal_food.cities` key on the city ID while everything else keys on its UID.
struct StorySiteId {
    std::string id;
    std::string uid;
};

struct StoryCityThreat {
    std::string city_uid;
    double war_pressure = 0.0;
    double wars_recent = 0.0;
    bool enemy_living = false;
    double war_risk = 0.0;
    double war_hunger = 0.0;
    double regional_threat = 0.0;
};

struct StoryRuralCore {
    std::string site_id;  // a city `id`
    double food_deficit = 0.0;
};

struct StoryFoodCoverage {
    std::string site_id;  // a city `id`
    double food_coverage = 0.0;  // 1.0 is self-feeding
};

struct StoryNodeIntensity {
    std::string node_id;
    double intensity = 0.0;
};

struct StoryWebRequest {
    std::uint64_t seed = 0;  // the initiative stagger, and nothing else
    // Must carry status "ok". A missing or failed block is a hard failure: compiling a web
    // against a cast that does not exist would produce a plausible, empty answer.
    const HeroesBlock* heroes = nullptr;

    std::vector<StorySiteId> site_ids;
    std::vector<StoryCityThreat> threats;
    std::vector<StoryRuralCore> cores;
    std::vector<StoryFoodCoverage> coverage;
    std::vector<StoryNodeIntensity> node_intensity;
    // A SINGLE occurrence anywhere in the almanac sets the flag for EVERY hero. The limits
    // section calls this deliberately coarse; it is not a per-city reading.
    bool lunar_surge = false;
    bool lunar_hollow_night = false;
};

// ---------------------------------------------------------------------------
// What comes out.
// ---------------------------------------------------------------------------

// The arithmetic behind a spoke's weight, exported so a consumer can re-derive it. The
// product is computed FROM THE ROUNDED PARTS, which is what makes that possible.
struct StoryBreakdown {
    double boosts = 1.0;
    std::vector<std::string> boosted_by;  // in the trope's own boost-table order
    double alignment_fit = 1.0;
    double archetype_fit = 1.0;
    double claim_fit = 1.0;
};

struct StorySpoke {
    std::string trope_id, name;
    double weight = 0.0;
    StoryBreakdown breakdown;
    double angle = 0.0;
};

// The one change a hero needs to reach another trope.
struct StoryThread {
    std::string to;
    double weight = 0.0;
    // open | alignment | claim | feature. An `open` thread whose pull opposes the hero is
    // REWRITTEN to `alignment` before the sort, so the rewrite moves its sort position.
    std::string condition_kind;
    std::string verb;                // claim
    std::vector<std::string> facts;  // feature
    std::string axis, toward;        // alignment
    std::string cut;                 // empty when the archetype permits it
};

struct StoryBoundOption {
    std::string id, text;
    HeroAxisPair delta;
    std::string effect;
};

struct StoryBoundAct {
    std::string id, title;
    int ring = 0;  // 1-based position; the catalogue's act order IS the ring order
    std::string prompt;
    std::string completion_kind, completion_target, completion_state;
    std::string abandonment_kind;
    int abandonment_days = 0;
    std::vector<std::string> implicit;
    std::vector<StoryBoundOption> options;
};

struct StoryConsidered {
    std::string trope_id;
    // The failing tokens of the conjunction closest to holding: fewest failures, then
    // catalogue order, the FIRST such conjunction winning.
    std::vector<std::string> missing;
};

struct StoryRest {
    std::string hook_trope_id, hook_act_id, hook_prompt;
    int expires_day = 0;
    std::vector<std::pair<std::string, double>> armed;  // the rest of the spokes, in order
    int initiative_day = 0;
};

struct StoryWebEntry {
    std::string uid, display_name, role, archetype;
    double law = 0.0, good = 0.0;
    std::string code;
    double angle = 0.0;
    std::string claim;  // the verb, or "none"
    std::vector<std::string> facts;  // sorted
    bool claim_locked = false;
    bool drift_locked = false;
    bool has_allowed_verbs = false;
    std::vector<std::string> allowed_verbs;
    std::vector<StorySpoke> spokes;  // sorted by (-weight, trope_id)
    std::string offered;             // spokes[0]
    StoryRest rest;
    std::vector<StoryBoundAct> acts;
    std::vector<StoryThread> threads;
    std::vector<StoryConsidered> considered;
    std::vector<std::string> examples;
};

// A hero no trope admits, with the nearest misses. Produces no acts, no threads and no
// initiative draw.
struct StoryUnwoven {
    std::string uid, display_name, archetype;
    std::vector<std::string> facts;
    std::vector<StoryConsidered> nearest;  // up to three
};

struct StoryTropeSummary {
    std::string id, name;
    HeroAxisPair pull;
    std::vector<std::string> cast;
    double angle = 0.0;
};

struct StoryWebSummary {
    int woven = 0, unwoven = 0, tropes = 0, reachable_tropes = 0;
    // trope id -> how many heroes were offered it, KEY-SORTED rather than insertion ordered.
    std::vector<std::pair<std::string, int>> offered;
    double mean_spokes = 0.0;  // 0.0, not NaN, when there are no webs
};

struct StoryWebBlock {
    int version = kStoryWebVersion;
    std::string status = "ok";
    std::string error;
    StoryCatalogueIdentity policy_revision{};
    // Copied from the hero block so a consumer can tell which cast a web was compiled
    // against. NOTHING CHECKS that the two came from the same run: a stale `heroes` with a
    // fresh `story_web` compiles silently.
    HeroCatalogueIdentity heroes_revision{};
    StoryWebSummary summary;
    std::vector<StoryWebEntry> webs;
    std::vector<StoryUnwoven> unwoven;
    std::vector<StoryTropeSummary> tropes;
    std::vector<std::string> unreachable_tropes;  // sorted
    std::string method;
    std::string limits;
};

// ---------------------------------------------------------------------------
// The pass, and the pieces a caller or a test may want on their own.
// ---------------------------------------------------------------------------

// A token holds when it is present, or when it starts '!' and its fact is ABSENT.
bool story_holds(std::string_view token, const std::set<std::string>& facts);
// Any conjunction holding entirely.
bool story_eligible(const StoryTrope& trope, const std::set<std::string>& facts);

// Good to the right, lawful up; 0..360 counter-clockwise from good.
double story_angle_degrees(double law, double good);

// A spoke pulling the way the hero already leans is favoured; an opposed one is damped,
// never removed.
double story_alignment_fit(const HeroAxisPair& pull, const HeroAlignment& alignment);

// The axis a pull opposes the hero hardest on, and which way it pulls there -- (axis,
// "up"|"down") -- or nothing when the pull agrees. An OPEN thread whose pull opposes is
// rewritten into an `alignment` one on this answer.
//
// Published because the tie-break is otherwise untestable: the two axis contributions tie
// exactly only for a pull and an alignment that happen to pair off, which is not something
// a test can arrange out of the shipped catalogue. On a tie the axes are compared
// ALPHABETICALLY, so `good` wins.
std::optional<std::pair<std::string, std::string>> story_opposing_axis(
    const HeroAxisPair& pull, const HeroAlignment& alignment);

StoryWebBlock resolve_story_web(const StoryWebRequest& request);

// The isolation contract, as in the hero generator: a failure is a reported block and the
// world still returns.
StoryWebBlock attach_story_web(const StoryWebRequest& request);

}  // namespace fwg
