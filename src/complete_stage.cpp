// Stage 16, "Simulation complete": the passes that read a finished world.
//
// The reference runs, in order: the city, hamlet and castle planners; the nomads, their
// routes, the beast movements, the encounters, and the nomads' effects on the world; then
// the heroes, the story web, the rosters and the key locations. The planners come first
// because they only read the world, and the bands come before the packages that read
// them, so heroes and rosters see a world with its nomads in it.
//
// Each pass is a function of a request, not of a World. This file is where the World is
// turned into those requests, which makes it the one place that has to know how the
// blocks the earlier stages wrote map onto what each pass reads.
#include "fwg/complete_stage.hpp"

#include <cmath>
#include <utility>

#include <set>

#include "fwg/beasts.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/entities.hpp"
#include "fwg/heroes.hpp"
#include "fwg/magic.hpp"
#include "fwg/nomads.hpp"
#include "fwg/story_web.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

std::vector<CitySite> city_sites(const World& world) {
    std::vector<CitySite> out;
    out.reserve(world.settlements.sites.size());
    for (const SettlementSite& s : world.settlements.sites) {
        CitySite site;
        site.uid = s.uid;
        site.id = s.id;
        site.name = s.name;
        site.x = s.x;
        site.z = s.z;
        site.city_class = s.city_class;
        site.population_profile = s.population_profile;
        site.population_estimate = s.population_estimate;
        site.urban_population_estimate = s.urban_population_estimate;
        // The planner never reads the distance, only whether one was measured: a city
        // with water it can collect or have delivered. Unreachable water is no water.
        if (std::isfinite(s.freshwater_distance_m)) site.freshwater_distance_m = s.freshwater_distance_m;
        site.resource_potential = s.resource_potential;
        site.founded_by_player = s.founded_by == "player";
        // Left empty: the planner projects the world's own roads into the city's window
        // once it has decided how wide the window is.
        out.push_back(std::move(site));
    }
    return out;
}

std::vector<HamletPin> hamlet_pins(const World& world) {
    std::vector<HamletPin> out;
    out.reserve(world.humans.hamlets.size());
    for (const RuralSite& h : world.humans.hamlets) {
        HamletPin pin;
        pin.id = h.id;
        pin.x = h.x;
        pin.z = h.z;
        pin.node = h.node;
        // A position in the settlement list, which is exactly what the planner indexes
        // with -- as long as the list it is handed is in the world's own order, which
        // city_sites() preserves. A re-sorted list would attribute every hamlet to the
        // wrong parent city, and the only symptom would be a plausible neighbour's name.
        pin.core_id = h.core_id;
        pin.population_profile = h.population_profile;
        pin.role = h.role;
        pin.access_nodes = h.access_nodes;
        out.push_back(std::move(pin));
    }
    return out;
}

// The nomad pass's view of the world. Ruins, villain claims and threat assessments are
// written by the age transitions, which are not wired yet, so those three are honestly
// empty -- a band's roll that reads them sees a world that has never had an age turn.
NomadRequest nomad_request(const World& world, const SphereGrid& grid,
                           const std::vector<NomadHabitatCell>& cells) {
    const Config& cfg = world.effective;
    NomadRequest request;
    request.world_recipe = cfg.world_recipe != 0;
    request.phase = cfg.phase;
    request.seed = cfg.seed;
    request.size = cfg.size;
    request.settlement_spacing_m = cfg.settlement_spacing;
    request.globe_radius_m = cfg.globe_radius;
    request.road_max_grade = cfg.road_max_grade;
    request.occurrence = cfg.options[Option::nomad_occurrence];
    request.density = cfg.options[Option::nomad_density];
    request.variation = cfg.options.as_int(Option::nomad_variation);
    request.magic_enabled = cfg.magic_enabled != 0;
    request.grid = &grid;
    request.cells = &cells;

    for (const SettlementSite& s : world.settlements.sites) {
        NomadSite site;
        site.uid = s.uid;
        site.id = std::to_string(s.id);
        site.node = s.node;
        site.x = s.x;
        site.z = s.z;
        site.direction = s.direction;
        site.population_estimate = static_cast<double>(s.population_estimate);
        site.urban_population_estimate = static_cast<double>(s.urban_population_estimate);
        site.rural_population_estimate = static_cast<double>(s.rural_population_estimate);
        site.absorbed_bands = s.absorbed_bands;
        site.absorbed_refugees = s.absorbed_refugees;
        request.sites.push_back(std::move(site));
    }
    for (const RuralSite& h : world.humans.hamlets) {
        NomadSite hamlet;
        hamlet.uid = h.uid;
        hamlet.id = h.id;
        hamlet.node = h.node;
        hamlet.x = h.x;
        hamlet.z = h.z;
        hamlet.direction = direction(h.x, h.z, cfg.size);
        hamlet.delivered_food = h.delivered_food;
        request.hamlets.push_back(std::move(hamlet));
    }
    // Harbours reach only the clearance list: a band will not camp on a quay.
    for (const HarbourPort& p : world.society.fisheries.ports) {
        NomadSite port;
        port.id = p.id;
        port.node = p.node;
        port.x = p.x;
        port.z = p.z;
        port.direction = direction(p.x, p.z, cfg.size);
        request.ports.push_back(std::move(port));
    }
    if (world.has_magic) request.ley = nomad_ley_nodes(world.magic);
    if (world.has_religion) {
        for (const ReligionSite& s : world.religion.sites) {
            if (s.node < 0) continue;
            NomadShrine shrine;
            shrine.id = s.id;
            shrine.god_id = s.god_id;
            shrine.node = s.node;
            shrine.x = grid.point_x[static_cast<std::size_t>(s.node)];
            shrine.z = grid.point_z[static_cast<std::size_t>(s.node)];
            shrine.direction = direction(shrine.x, shrine.z, cfg.size);
            request.shrines.push_back(std::move(shrine));
        }
        for (const GodRecord& g : world.religion.gods) request.gods.push_back({g.id, g.schools});
    }
    for (const Road& road : world.roads.routes) request.roads.push_back({road.nodes, road.length_m});
    for (const SeasonalMonth& m : world.seasonal_months) request.seasonal_temperature.push_back(&m.temperature);
    return request;
}

// A band as the encounter pass sees it: where it is, what it is, and where it camps.
std::vector<EncounterBand> encounter_bands(const NomadBlock& block) {
    std::vector<EncounterBand> out;
    out.reserve(block.groups.size());
    for (const NomadBand& b : block.groups) {
        EncounterBand band;
        band.uid = b.uid;
        band.node = b.node;
        band.classification = std::string(to_string(b.classification));
        band.disposition = std::string(to_string(b.disposition));
        band.size = b.size;
        for (const std::string_view c : b.carries) band.carries.emplace_back(c);
        for (const std::string_view s : b.seeks) band.seeks.emplace_back(s);
        band.god_id = b.god_id;
        band.speed_m_per_day = b.speed_m_per_day;
        band.column_length_m = b.column_length_m;
        for (const NomadCamp& c : b.camps) {
            band.camps.push_back({c.id, c.node, c.direction, std::string(to_string(c.kind)), c.arrive_day, c.depart_day});
        }
        band.has_legs = !b.legs.empty();
        out.push_back(std::move(band));
    }
    return out;
}

// The bands, the herds, and what a traveller would meet. The reference's order, which is
// load-bearing: the herds are routed after the bands so a follower can attach to a band's
// camp, the encounters read both, and the effects run last because they read the routes.
void add_the_moving_world(World& world, const SphereGrid& grid) {
    const Config& cfg = world.effective;
    const std::vector<NomadHabitatCell> cells = nomad_habitat_cells(world.layers, grid);
    const NomadRequest request = nomad_request(world, grid, cells);

    std::optional<NomadBlock> nomads = add_nomads(request);
    if (nomads) {
        nomads->routes = add_nomad_routes(*nomads, request);
        world.systems.push_back("nomads");
    }
    const std::vector<EncounterBand> bands = nomads ? encounter_bands(*nomads) : std::vector<EncounterBand>{};

    BeastMovementRequest herd;
    herd.world_recipe = cfg.world_recipe;
    herd.phase = cfg.phase;
    herd.layers = &world.layers;
    herd.grid = &grid;
    herd.globe_radius_m = cfg.globe_radius;
    herd.settlement_spacing_m = cfg.settlement_spacing;
    herd.road_max_grade = cfg.road_max_grade;
    // Two seeds, and the difference matters: the per-year irruption decision reads the
    // GENESIS seed, so it is a property of the year and not of how far the clock stepped.
    herd.seed = cfg.seed;
    herd.genesis_seed = cfg.seed;
    herd.share = cfg.options[Option::beast_movement_share];
    herd.nomad_variation = cfg.options.as_int(Option::nomad_variation);
    // No clock exists yet, so the year is where founding ended -- the reference's own
    // fallback when a world has no world_clock.
    herd.year = world.settlements.founding.end_year;
    herd.wildlife = world.wildlife.sites;
    herd.beast_nests = world.beast_nests.sites;
    herd.bands = bands;
    if (std::optional<BeastMovements> herds = add_beast_movements(herd)) {
        world.beast_movements = std::move(*herds);
        world.has_beast_movements = true;
        world.systems.push_back("beast_movements");
    }

    EncounterRequest meet;
    meet.world_recipe = cfg.world_recipe;
    meet.phase = cfg.phase;
    meet.bands = bands;
    meet.groups = world.beast_movements.groups;
    if (std::optional<EncounterIndex> index = add_encounters(meet)) {
        world.encounters = std::move(*index);
        world.systems.push_back("encounters");
    }

    if (nomads) {
        if (std::optional<NomadEffects> effects = apply_nomad_effects(*nomads, request)) {
            // A cult deepens its circuit node; the reference edits the network and does
            // NOT re-evaluate the fields, so the published ley layers stay as they were
            // until the next evaluation -- an age turn. Faithful to that here.
            for (const NomadLeyEdit& edit : effects->leyline_edits) {
                const auto net = world.magic.networks.find(edit.school);
                if (net == world.magic.networks.end()) continue;
                for (LeyNode& node : net->second.nodes) {
                    if (node.id == edit.node_id) node.intensity = edit.intensity;
                }
            }
            world.nomad_effects = std::move(*effects);
            world.systems.push_back("nomad_effects");
        }
        world.nomads = std::move(*nomads);
        world.has_nomads = true;
    }
}

// The hero generator's view of the world.
//
// Two id spaces meet here and must not be crossed. A city's `uid` is its stable identity;
// its `id` is its position in the settlement list, and the hinterland, the shipments, the
// colleges and the ports all name their city by that position. The request carries both,
// as strings, exactly as the reference hands them over.
HeroesRequest heroes_request(const World& world) {
    const Config& cfg = world.effective;
    HeroesRequest request;
    request.seed = cfg.seed;
    request.final_age = world.age;
    request.globe_radius_m = cfg.globe_radius;
    request.spacing_m = cfg.settlement_spacing;
    request.layers = &world.layers;

    for (const SettlementSite& s : world.settlements.sites) {
        HeroCity city;
        city.uid = s.uid;
        city.id = std::to_string(s.id);
        city.name = s.name;
        city.city_class = s.city_class == CityClass::Capital  ? "capital"
                          : s.city_class == CityClass::Medium ? "medium"
                                                              : "small";
        city.civilization_id = s.population_profile;
        city.population_profile = s.population_profile;
        city.parent_race_id = s.parent_race_id;
        city.node = s.node;
        city.x = s.x;
        city.z = s.z;
        city.direction = s.direction;
        city.suitability = s.suitability;
        city.founded_age = s.founded_age;
        city.founding_capital = s.founding_capital;
        city.diaspora_reason = s.diaspora_reason;
        city.source_civilization_id = s.source_civilization_id;
        request.cities.push_back(std::move(city));
    }

    std::set<std::string> peoples;
    for (const SettlementSite& s : world.settlements.sites) peoples.insert(s.population_profile);
    for (const std::string& id : peoples) {
        const CivilizationProfile* profile = civilization_profile(id);
        const EntityRules* rules = entity_rule(id);
        request.civilizations.push_back({id, profile != nullptr ? std::string(profile->name) : id,
                                         rules != nullptr ? std::string(rules->parent_race_id) : std::string()});
    }

    for (const CultureGroup& c : world.humans.cultures) {
        HeroCulture culture;
        culture.id = c.id;
        culture.civilization_id = c.civilization_id;
        for (const int city : c.city_ids) culture.city_ids.push_back(std::to_string(city));
        request.cultures.push_back(std::move(culture));
    }

    for (const NestSite& n : world.beast_nests.sites) {
        HeroNest nest;
        nest.id = n.id;
        nest.name = std::string(n.name);
        nest.family = std::string(n.family);
        nest.tier = n.tier;
        nest.has_tier = true;
        nest.range_m = n.range_m;
        nest.has_range = true;
        nest.has_grid = true;
        nest.x = n.x;
        nest.z = n.z;
        nest.direction = n.direction;
        // The reference's nest rows carry no node, so every slay hook it writes reads
        // `target_has_no_node` and no player is ever pointed at a lair. The node is known
        // here, and supplying it is what the port's optional field is for.
        nest.node = n.node;
        request.nests.push_back(std::move(nest));
    }

    for (const RuralSite& h : world.humans.hamlets) {
        request.hamlets.push_back({h.id, h.kind, h.role, std::to_string(h.core_id), h.node, h.x, h.z});
    }
    for (const RuralSite& f : world.humans.fortresses) {
        request.fortresses.push_back({f.id, std::to_string(f.core_id), f.node, f.x, f.z, f.defence_score});
    }
    for (const CityCore& c : world.humans.cores) {
        request.cores.push_back({std::to_string(c.site_id), c.food_deficit});
    }
    for (const FoodShipment& s : world.humans.shipments) {
        request.shipments.push_back({std::to_string(s.from), std::to_string(s.to)});
    }
    for (const College& c : world.colleges.colleges) {
        request.colleges.push_back({c.id, std::to_string(c.core_id), c.node, c.x, c.z});
    }

    // In the magic school table's order, not the map's: a std::map walks alphabetically,
    // and the reference builds its node-to-school and node-to-intensity maps from one
    // ordering. Two orderings are how those two maps come to disagree.
    for (const MagicSchool& school : magic_school_table()) {
        const auto net = world.magic.networks.find(std::string(school.name));
        if (net == world.magic.networks.end()) continue;
        HeroLeyNetwork network;
        network.school = net->first;
        for (const LeyNode& n : net->second.nodes) network.nodes.push_back({n.id, n.direction, n.intensity});
        request.networks.push_back(std::move(network));
    }

    for (const GodRecord& g : world.religion.gods) request.gods.push_back({g.id, g.name});
    for (const ReligionSite& s : world.religion.sites) {
        request.religion_sites.push_back({s.id, s.kind, s.god_id, s.ruin_id, s.node, s.born_under_surge});
    }
    for (const HarbourPort& p : world.society.fisheries.ports) {
        request.ports.push_back({p.id, std::to_string(p.core_id), p.node, p.sea_node, p.trade_terminal});
    }
    for (const TransportRoute& r : world.society.transport.routes) {
        if (r.mode != "sea") continue;
        request.sea_routes.push_back({{r.port_a, r.port_b}});
    }
    // Ruins, wars and threat assessments are written by the age transitions, which are not
    // wired yet. Empty is the honest answer: a cast drawn from a world that has never had
    // an age turn.
    return request;
}

StoryWebRequest story_web_request(const World& world, const HeroesBlock& heroes) {
    StoryWebRequest request;
    request.seed = world.effective.seed;
    request.heroes = &heroes;
    for (const SettlementSite& s : world.settlements.sites) request.site_ids.push_back({std::to_string(s.id), s.uid});
    for (const CityCore& c : world.humans.cores) request.cores.push_back({std::to_string(c.site_id), c.food_deficit});
    for (const SeasonalCity& c : world.seasonal_food.cities) {
        request.coverage.push_back({std::to_string(c.site_id), c.food_coverage});
    }
    for (const auto& [school, net] : world.magic.networks) {
        for (const LeyNode& n : net.nodes) request.node_intensity.push_back({n.id, n.intensity});
    }
    // One occurrence anywhere in the almanac sets the flag for every hero -- deliberately
    // coarse, and the reference's own reading.
    for (const AlmanacEvent& e : world.lunar_almanac.events) {
        request.lunar_surge = request.lunar_surge || e.kind == "surge";
        request.lunar_hollow_night = request.lunar_hollow_night || e.kind == "hollow_night";
    }
    return request;
}

}  // namespace

void add_simulation_complete(World& world) {
    if (!world.has_settlements) return;
    const auto held = sphere_grid(world.effective.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;

    const std::vector<CitySite> sites = city_sites(world);
    world.city_plans = fill_cities(world, grid, sites);
    world.systems.push_back("city_plans");
    if (world.has_humans) {
        world.hamlet_plans = fill_hamlets(world, grid, sites, hamlet_pins(world));
        world.systems.push_back("hamlet_plans");
    }
    world.has_plans = true;

    // Bands are part of the world, so they are placed before the packages that only read
    // it: heroes, the story web and the rosters see a world with its nomads in it.
    if (world.has_humans) add_the_moving_world(world, grid);

    // The cast, then the web of stories between them. The web reads only the heroes and
    // the world, and a web compiled against a cast that failed would be a plausible,
    // empty answer -- so a failed cast stops it rather than feeding it.
    world.heroes = attach_heroes(heroes_request(world));
    world.has_heroes = true;
    world.systems.push_back("heroes");
    world.story_web = attach_story_web(story_web_request(world, world.heroes));
    world.systems.push_back("story_web");
}

}  // namespace fwg
