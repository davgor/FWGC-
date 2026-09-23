// Stage 13, "Beasties and animals": the nests, and the faiths a world with nests in it
// comes to hold.
//
// The reference runs, in order, the two nest passes, the threat assessment, and religion.
// Religion is last because it reads everything before it: which peoples founded, which
// god a diaspora carried, what lives in the wild to be feared. The threat assessment is
// not ported yet and slots in between; nothing here reads it.
#include "fwg/beasties_stage.hpp"

#include <map>
#include <string>

#include "fwg/grid.hpp"
#include "fwg/religion.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

// Religion's view of the world, built from the blocks the earlier stages wrote.
//
// Adapters live here rather than in religion.cpp because religion is a function of a
// request, not of a World, and it should stay one: an age transition re-resolves the
// pantheon from a world that is half rebuilt, and a visitation resolves it from a world
// with a god walking in it.
ReligionRequest religion_request(const World& world) {
    ReligionRequest request;
    request.seed = world.config.seed;
    request.magic = world.has_magic ? &world.magic : nullptr;
    request.layers = &world.layers;
    request.almanac = world.has_astrology ? &world.lunar_almanac : nullptr;
    request.globe_radius_m = world.effective.globe_radius;
    request.lunar_influence = world.effective.options[Option::lunar_influence];

    std::map<int, const SettlementSite*> by_node;
    for (const SettlementSite& s : world.settlements.sites) {
        by_node[s.node] = &s;
        ReligionCity city;
        city.uid = s.uid;
        city.population_profile = s.population_profile;
        city.x = s.x;
        city.z = s.z;
        city.node = s.node;
        city.direction = s.direction;
        city.diaspora = s.diaspora;
        request.cities.push_back(std::move(city));
    }

    request.used_civilizations = world.settlements.founding.used_civilizations;

    // A founding event does not carry its diaspora reason or its year -- the city record
    // does -- so each event is joined to the city standing on its node. A schism is a
    // fact a faith can be founded on, and an event without its reason would lose it.
    for (const FoundingEvent& e : world.settlements.founding.events) {
        if (e.node < 0) continue;
        ReligionFoundingEvent event;
        event.status = e.status;
        event.population_profile = e.population_profile;
        event.node = e.node;
        if (const auto it = by_node.find(e.node); it != by_node.end()) {
            event.diaspora_reason = it->second->diaspora_reason;
            event.founding_year = it->second->founding_year;
        }
        request.founding_events.push_back(std::move(event));
    }

    // Monster nests only. Religion fears what could destroy a city; the animal pass is the
    // world's fauna, and a people does not raise a god against deer.
    for (const NestSite& n : world.beast_nests.sites) {
        ReligionNest nest;
        nest.id = n.id;
        nest.name = std::string(n.name);
        nest.family = std::string(n.family);
        nest.real = n.real;
        nest.tier = n.tier;
        nest.range_m = n.range_m;
        nest.direction = n.direction;
        request.nests.push_back(std::move(nest));
    }

    if (world.has_water) {
        request.ocean_km2 = world.water.ocean_km2;
        request.lake_km2 = world.water.lake_km2;
        request.dry_km2 = world.water.dry_km2;
    }
    // Sky islands are suspended on recipe 3, and ruins and ages are written only by the
    // age transitions, which have not run at stage 13 -- all three are honestly empty.
    return request;
}

}  // namespace

void add_beasties(World& world) {
    if (!world.has_settlements) return;

    const auto held = sphere_grid(world.effective.size, world.effective.globe_radius);
    std::vector<int> settled;
    settled.reserve(world.settlements.sites.size());
    for (const SettlementSite& s : world.settlements.sites) settled.push_back(s.node);
    if (auto passes = add_nests(world, *held, settled)) {
        world.wildlife = std::move(passes->wildlife);
        world.beast_nests = std::move(passes->beast_nests);
        world.has_wildlife = true;
        world.systems.push_back("wildlife");
    }

    // Religion reads the nests just placed. It needs the moon for the turning-moon god
    // and the ley fields for every school a god answers to; on a world without magic or
    // without a sky there is still a pantheon, only a narrower one.
    world.religion = resolve_religion(religion_request(world));
    world.has_religion = true;
    world.systems.push_back("religion");
}

}  // namespace fwg
