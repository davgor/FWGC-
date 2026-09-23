// Stage 13, "Beasties and animals": the nests, the threat each city reads for its walls,
// and the faiths a world with nests in it comes to hold.
//
// The reference runs, in order, the two nest passes, the threat assessment, and religion.
// Religion is last because it reads everything before it: which peoples founded, which
// god a diaspora carried, what lives in the wild to be feared. Every age transition runs
// the same three again over the world it rebuilt, through the same functions.
#include "fwg/beasties_stage.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>

#include "fwg/grid.hpp"
#include "fwg/magic.hpp"
#include "fwg/religion.hpp"
#include "fwg/world.hpp"

namespace fwg {

WarCity war_city(const SettlementSite& s) {
    WarCity city;
    city.uid = s.uid;
    city.id = s.id;
    city.node = s.node;
    city.x = s.x;
    city.z = s.z;
    city.direction = s.direction;
    city.name = s.name;
    city.population_profile = s.population_profile;
    city.parent_race_id = s.parent_race_id;
    city.city_class = s.city_class;
    city.population_estimate = s.population_estimate;
    city.war_history = s.war_history;
    return city;
}

std::vector<WarCity> war_cities(const World& world) {
    std::vector<WarCity> out;
    out.reserve(world.settlements.sites.size());
    for (const SettlementSite& s : world.settlements.sites) out.push_back(war_city(s));
    return out;
}

WarGround war_ground(const World& world) {
    WarGround ground;
    ground.globe_radius_m = world.effective.globe_radius;
    ground.settlement_spacing_m = world.effective.settlement_spacing;
    ground.cities = war_cities(world);
    ground.cores = world.has_humans ? &world.humans.cores : nullptr;
    ground.routes = world.has_roads ? &world.roads.routes : nullptr;
    if (world.has_villains) ground.villains = world.villains.people;
    return ground;
}

std::vector<ReligionNest> monster_nests(const World& world) {
    std::vector<ReligionNest> out;
    out.reserve(world.beast_nests.sites.size());
    for (const NestSite& n : world.beast_nests.sites) {
        ReligionNest nest;
        nest.id = n.id;
        nest.name = std::string(n.name);
        nest.family = std::string(n.family);
        nest.real = n.real;
        nest.tier = n.tier;
        nest.range_m = n.range_m;
        nest.direction = n.direction;
        out.push_back(std::move(nest));
    }
    return out;
}

ThreatRequest threat_request(const World& world, const std::string& evaluated_after, std::optional<int> age) {
    ThreatRequest request;
    request.evaluated_after = evaluated_after;
    request.age = age;
    request.ground = war_ground(world);
    request.layers = &world.layers;
    request.nests = monster_nests(world);
    if (world.has_magic) {
        request.dominance_threshold = world.magic.mutation_threshold;
        request.dominance_margin = world.magic.dominance_margin;
    }
    return request;
}

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
    // The pantheon it replaces: a sleeping god keeps its last aspect, a hidden god that
    // returned stays walking, and theophanies and conversions carry forward.
    request.previous = world.has_religion ? &world.religion : nullptr;

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
    request.nests = monster_nests(world);

    // What the ages left: a city that destroyed itself raises a Weave cult, a war a shrine to
    // the Red Field; and every age's wars count toward the gods of strife.
    for (const Ruin& ruin : world.ruins) {
        request.ruins.push_back({ruin.id, ruin.node, ruin.cause, ruin.destroyed_age, ruin.population_profile});
    }
    const std::size_t weave = static_cast<std::size_t>(magic_school_index("weave"));
    for (const AgeRecord& age : world.history.ages) {
        ReligionAge row;
        row.age = age.age;
        row.wars = static_cast<int>(age.wars.size());
        if (age.moon) row.weave_tide = age.moon->tide[weave];
        request.ages.push_back(row);
    }

    if (world.has_water) {
        request.ocean_km2 = world.water.ocean_km2;
        request.lake_km2 = world.water.lake_km2;
        request.dry_km2 = world.water.dry_km2;
    }
    // Sky islands are suspended on recipe 3, so there are none to count.
    return request;
}

std::vector<int> nest_settled_nodes(const World& world) {
    // The reference's `settled_directions`, in its order: the cities, then the harbours, then
    // the hamlets -- which carry every harbour a second time, because stage 12 appends each
    // port to the rural list. The duplicates are the reference's and are kept: this list is a
    // tie-break order (see add_nests), and a nearest distance cannot change for a point
    // listed twice. Fortresses and colleges are not in it, in the reference or here.
    std::vector<int> settled;
    settled.reserve(world.settlements.sites.size() + world.society.fisheries.ports.size() +
                    world.humans.hamlets.size());
    for (const SettlementSite& s : world.settlements.sites) settled.push_back(s.node);
    for (const HarbourPort& p : world.society.fisheries.ports) settled.push_back(p.node);
    for (const RuralSite& h : world.humans.hamlets) settled.push_back(h.node);
    return settled;
}

void place_nests(World& world, std::optional<int> evaluated_age) {
    const auto held = sphere_grid(world.effective.size, world.effective.globe_radius);
    // Everything standing on the surface, not the cities alone. Handed only the cities, the
    // passes laired 472 of 3679 monsters inside a hamlet's or a harbour's clearance on the
    // seed-42 size-65 world, where the reference's world for that request lairs none, and
    // 332 of the 365 cells a logging camp qualified for were already held, against 225 once
    // the lairs keep clear of the hamlets and harbours too.
    const std::vector<int> settled = nest_settled_nodes(world);
    // Every lair a beast ruin names STANDS (2026-09-23): the monster that razed a city keeps
    // its lair through every later placement, which is what lets its dread still be living
    // there at stage 16. The lair is read off the placement this one replaces, so a lair
    // dropped once -- settled over, or purged by a god -- is gone for good.
    std::set<std::string> razers;
    for (const Ruin& ruin : world.ruins) {
        if ((ruin.cause == "dragon" || ruin.cause == "monster") && !ruin.evidence.nest_id.empty()) {
            razers.insert(ruin.evidence.nest_id);
        }
    }
    std::vector<NestSite> standing;
    if (world.has_wildlife) {
        for (const NestSite& site : world.beast_nests.sites) {
            if (razers.count(site.id) != 0) standing.push_back(site);
        }
    }
    if (auto passes = add_nests(world, *held, settled, standing)) {
        world.wildlife = std::move(passes->wildlife);
        world.beast_nests = std::move(passes->beast_nests);
        world.beast_nests.evaluated_age = evaluated_age;
        world.has_wildlife = true;
        world.systems.push_back("wildlife");
    }
}

void assess_world_threats(World& world, const std::string& evaluated_after, std::optional<int> age) {
    world.threat_assessments = assess_threats(threat_request(world, evaluated_after, age));
    world.has_threat_assessments = true;
    world.systems.push_back("threat_assessments");
}

void resolve_world_religion(World& world) {
    // The request points into world.religion as `previous`; the resolve returns a new value
    // before anything is assigned, so the pointer is valid for as long as it is read.
    Religion resolved = resolve_religion(religion_request(world));
    world.religion = std::move(resolved);
    world.has_religion = true;
    world.systems.push_back("religion");
}

void add_beasties(World& world) {
    if (!world.has_settlements) return;
    place_nests(world, std::nullopt);
    // Between the nests and the faiths, as the reference runs it: the nests just placed are
    // half of what a city fears, and religion reads none of this.
    assess_world_threats(world, "beast_nests", std::nullopt);
    // Religion reads the nests just placed. It needs the moon for the turning-moon god
    // and the ley fields for every school a god answers to; on a world without magic or
    // without a sky there is still a pantheon, only a narrower one.
    resolve_world_religion(world);
}

}  // namespace fwg
