// The record the hero-action API (hero_action.hpp) appends to a world's operation history.
//
// Split from hero_action.hpp (the API) for the reason person_record.hpp is split from
// person_state.hpp: world.hpp holds these through ages.hpp's HistoryOperation, and the API's
// result type contains a World.
#pragma once

#include <string>
#include <vector>

namespace fwg {

inline constexpr int kHeroActionApiVersion = 1;

// What a hero did, as the request said it and the log keeps it.
struct HeroActionEntry {
    std::string uid;       // the hero
    std::string action;    // defended | hunted | assaulted
    std::string outcome;   // won | lost
    // Where it was done: a city, hamlet, fortress, ruin or key location the world names. Needed to
    // defend or assault a place; for a hunt, where the hunt went, or empty.
    std::string place_uid;
    // What they faced: a nest, a dread or another hero the world names. Needed for a hunt; to defend
    // or assault a place, whoever stood against them there, or empty.
    std::string against_uid;
    double day = 0.0;  // the world's day it was done: at or after the founding, never past the clock
    // Where they stand after, when they moved: a city, hamlet, fortress or ruin the world names, and
    // how they stand there, one of story_situations(). Both empty: where they stood.
    std::string moved_to_uid, moved_to_situation;
};

// The hero route's entry: api_version, kind "hero", the actions as they applied, in call order.
struct HeroOperation {
    int api_version = kHeroActionApiVersion;
    std::vector<HeroActionEntry> actions;
};

inline constexpr int kHeroShiftApiVersion = 1;
// The most one shift moves a hero along an axis: the pole cut's own size (hero_alignment_record puts the poles
// past 0.34 either way), half the neutral band, so one step can carry someone across a boundary and never across
// two.
inline constexpr double kHeroShiftStep = 0.34;

// A hero's alignment moved after a deed of theirs, as the request said it and the log keeps it.
struct HeroShiftEntry {
    std::string uid;               // the hero, of one face
    double law = 0.0, good = 0.0;  // along each axis, each at most kHeroShiftStep either way, not both zero
    std::string after_event_id;    // the deed of theirs it came after: one of their "action" deeds
    double day = 0.0;              // at or after the founding, never past the clock
};

// The hero-shift route's entry: api_version, kind "hero shift", the shifts as they applied, in call order.
struct HeroShiftOperation {
    int api_version = kHeroShiftApiVersion;
    std::vector<HeroShiftEntry> shifts;
};

}  // namespace fwg
