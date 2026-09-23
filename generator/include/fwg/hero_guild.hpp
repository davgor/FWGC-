// Hero guild planning, version 1: demand for one existing core guild hall.
//
// A port of the reference's fantasy_world_generator.hero_guild. An additive calculator,
// separate from generated state: it creates no NPC, changes no population, places no
// building and reads no generator stream. It estimates membership, activity, service and
// beds for the guild hall a city preset already contains.
//
// A request is exactly {civilization_id, city_block, population, visiting_heroes_requested,
// available_jobs, already_housed_resident_heroes, available_visitor_beds}. city_block is
// small_city, medium_city or capital_city; every count is an exact integer 0..10^9 --
// booleans, floats and strings refused. A policy is exactly the eight fields of
// data/hero-guild-policies.json; the packaged one for the civilization is used unless the
// caller supplies a complete policy, and the plan says which (`policy_source`, and the
// packaged `policy_revision` or null).
//
// In this order, all integer arithmetic:
//   resident  = min(max_resident_heroes, population * numerator // denominator)
//   visiting  = min(visiting_heroes_requested, max_visiting_heroes)
//   parties   = min(present // party_size, max_active_parties, available_jobs)
//   extra     = min(max_additional_staff, max(0, ceil(present / heroes_per_service_worker) - 2))
// The hall's cook and bartender are already counted by the city planner, so extra staff
// start above two, and the preset must carry exactly one unconditional hall with exactly
// that roster housed in the city -- the model refuses anything else rather than guess.
// Visitors need temporary beds, never permanent ones.
//
// Refusals are PatchError.
#pragma once

#include <cstdint>

#include "fwg/json_value.hpp"

namespace fwg {

inline constexpr std::int64_t kGuildMaxCount = 1'000'000'000;

// A fresh, validated copy of the packaged policies.
JsonValue guild_policy_document();

// The reference's `calculate(request, policy=None)`: a hero-guild-plan document.
JsonValue plan_hero_guild(const JsonValue& request, const JsonValue* policy = nullptr);

}  // namespace fwg
