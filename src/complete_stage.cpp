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

#include "fwg/beasts.hpp"
#include "fwg/city_plan.hpp"
#include "fwg/nomads.hpp"
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
}

}  // namespace fwg
