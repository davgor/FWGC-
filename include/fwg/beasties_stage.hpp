// Stage 13, "Beasties and animals": the wildlife and monster nests, and the faiths a
// world with monsters in it comes to hold.
#pragma once

namespace fwg {

struct World;

// Runs stage 13. Needs stage 10's cities: the nests stay clear of where people already
// are, and religion reads which peoples founded and what they have to fear.
void add_beasties(World& world);

}  // namespace fwg
