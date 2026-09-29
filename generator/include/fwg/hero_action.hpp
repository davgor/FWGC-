// The hero-action API: what a hero did after the world was made, written into their record
// (heroes version 5).
//
// Whatever decides what heroes do and how it goes does so outside the world; this verb records
// it. For each action, in order: a deed of event_kind "action" (where, against whom or what, the
// day, and how it went), a line of the hero's log in the world's own words, their fame and band
// recomputed with the deed by the formula generation uses (hero_fame_of), every deed's scope and
// the one they are known for marked again, and, when they moved, where they stand after.
//
// It takes the world by const reference and returns a new one; the caller's world is never
// touched. Nothing else is redrawn: the story web, the quest board and the other people keep what
// they had, as after the other world-level verbs. A call appends one HeroOperation to
// history.operations.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fwg/hero_record.hpp"
#include "fwg/world.hpp"

namespace fwg {

inline constexpr std::size_t kMaxHeroActions = 64;

struct HeroActionRequest {
    int api_version = kHeroActionApiVersion;
    std::vector<HeroActionEntry> actions;  // applied in order, 1 .. kMaxHeroActions
};

// One action as the call wrote it.
struct HeroActionReport {
    std::string uid;
    std::string event_id;  // the deed's id: action-<n>, n counting every action deed the world holds
    std::string text;      // the log's line
    std::string scope;     // how far word of the deed went
    bool known_for = false;  // whether it is now the deed they are known for
    double fame_before = 0.0, fame_after = 0.0;
    std::string band_before, band_after;
};

struct HeroActionResult {
    World world;
    std::vector<HeroActionReport> actions;
};

// Refused, naming the field (WorldTimeError, world_time.hpp's envelope), when: api_version is not
// this build's; there is no action or more than kMaxHeroActions; the world has no heroes block, or
// it failed; a uid names no hero, or a legend (who acts in stories only); an action or an outcome
// is not one of its words; a place or an against the world does not name, or one the action needs
// is missing; a day before the founding or past the clock; a moved-to site the world does not name
// as a city, hamlet, fortress or ruin, a situation not in story_situations(), or one without the
// other. Everything is judged before anything is written.
HeroActionResult hero_action_request(const World& world, const HeroActionRequest& request);

// ---- How a hero's alignment moved after a deed of theirs (heroes version 6) ---------------------
//
// Whatever decides how a hero takes what befell them does so outside the world; this verb records what
// it made of them. For each shift, in order: their alignment moved along each axis, clamped to -1..1 and
// recorded as generation records one (hero_alignment_record), their persona and their one face composed
// again from their card for the alignment they now have (compose_persona), and a line of their log in the
// world's words after the deed it came after. Nothing else is touched. A call appends one
// HeroShiftOperation to history.operations.

inline constexpr std::size_t kMaxHeroShifts = 64;

struct HeroShiftRequest {
    int api_version = kHeroShiftApiVersion;
    std::vector<HeroShiftEntry> shifts;  // applied in order, 1 .. kMaxHeroShifts
};

// One shift as the call wrote it.
struct HeroShiftReport {
    std::string uid;
    std::string code_before, code_after;  // their alignment's two letters before and after ("LN", "NE", ...)
    std::string text;                     // the log's line
};

struct HeroShiftResult {
    World world;
    std::vector<HeroShiftReport> shifts;
};

// Refused, naming the field (WorldTimeError), when: api_version is not this build's; there is no shift or
// more than kMaxHeroShifts; the world has no heroes block, or it failed; a uid names no hero, a legend, a
// hero of more than one face, or one whose archetype has no card; an axis moved past kHeroShiftStep either
// way, not a finite number, or both axes still; an after_event_id that is none of the hero's action deeds;
// a day before the founding or past the clock. Everything is judged before anything is written.
HeroShiftResult hero_shift_request(const World& world, const HeroShiftRequest& request);

}  // namespace fwg
