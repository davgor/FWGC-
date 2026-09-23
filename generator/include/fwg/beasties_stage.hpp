// Stage 13, "Beasties and animals": the wildlife and monster nests, the standing threat
// each city reads for its walls, and the faiths a world with monsters in it comes to hold.
//
// Every age transition re-runs all three over the world it rebuilt, so the passes and the
// World views they read are exported here rather than kept file-local: one adapter per
// request, whoever calls it.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fwg/fates.hpp"
#include "fwg/religion.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/wars.hpp"

namespace fwg {

struct World;

// Runs stage 13. Needs stage 10's cities: the nests stay clear of where people already
// are, and religion reads which peoples founded and what they have to fear.
void add_beasties(World& world);

// Both nest passes over the cities standing now. `evaluated_age` stamps the beast nests.
void place_nests(World& world, std::optional<int> evaluated_age);

// The standing threat of every city, written to world.threat_assessments.
// `evaluated_after` is "beast_nests" at stage 13 and "age" after an age's rebuild.
void assess_world_threats(World& world, const std::string& evaluated_after, std::optional<int> age);

// Re-resolve the pantheon from the world as it stands.
void resolve_world_religion(World& world);

// --- the World views ------------------------------------------------------

// A city as the wars, the fate lottery and the threat assessment read one, and every city
// in settlement-site order.
WarCity war_city(const SettlementSite& site);
std::vector<WarCity> war_cities(const World& world);

// The ground contention is measured on: the cities, their hinterland cores and roads,
// and the villains of the world's block (the passes that read it apply the standing
// filter themselves).
WarGround war_ground(const World& world);

// The monster nests, as the shared nest-threat reader takes them. The animal pass is the
// world's fauna and threatens nobody.
std::vector<ReligionNest> monster_nests(const World& world);

ThreatRequest threat_request(const World& world, const std::string& evaluated_after, std::optional<int> age);
ReligionRequest religion_request(const World& world);

}  // namespace fwg
