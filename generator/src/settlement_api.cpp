// Found a settlement the player chose. See settlement_api.hpp for the contract; this file is
// the reference's terrain_settlement_api over the World, in its order: judge the request,
// judge the ground, copy the world, write the site or the rural row, re-rank the cities,
// rebuild what was asked for, report, record -- then, where the world carries stage 16,
// redraw it around the new settlement.
#include "fwg/settlement_api.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fwg/beasties_stage.hpp"
#include "fwg/civilizations.hpp"
#include "fwg/complete_stage.hpp"
#include "fwg/entities.hpp"
#include "fwg/grid.hpp"
#include "fwg/json.hpp"  // format_double
#include "fwg/populated_stage.hpp"
#include "fwg/request_error.hpp"
#include "fwg/time.hpp"  // dedupe_warnings
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr std::string_view kPlayer = "player";

// Land ice: the one natural biome no civilization settles.
constexpr int kLandIceBiome = 17;

// How far a rural site the player raises is kept from anything already standing. The
// hinterland pass spaces generated ones by exactly these metres (humans.cpp), and the
// reference's founding API restates them the same way.
constexpr double kHamletSpacingM = 120.0;
constexpr double kFortressSpacingM = 200.0;

std::vector<std::string> kinds() { return {"city", "hamlet", "fortress"}; }
std::vector<std::string> roles() { return {"farming", "resource"}; }
std::vector<std::string> layouts() { return {"player", "generated"}; }
std::vector<std::string> rebuilds() { return {"humans", "none"}; }

bool listed(const std::vector<std::string>& list, const std::string& item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

// Python's str.strip() leaves nothing: ASCII whitespace only.
bool blank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    });
}

std::string fixed(double value, int places) {
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.*f", places, value);
    return std::string(buffer.data());
}

// The reference's `_layer`: the value at (x, z), or nothing where the world has no such layer.
std::optional<double> layer_value(const World& world, const std::string& name, int x, int z) {
    const auto it = world.layers.find(name);
    if (it == world.layers.end() || it->second.empty()) return std::nullopt;
    return it->second.at(x, z);
}

double layer_or(const World& world, const std::string& name, int x, int z, double fallback) {
    return layer_value(world, name, x, z).value_or(fallback);
}

void dedupe_systems(std::vector<std::string>& systems) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (std::string& name : systems) {
        if (seen.insert(name).second) out.push_back(std::move(name));
    }
    systems = std::move(out);
}

struct Checked {
    int node = 0;
    std::string kind;
    std::string role;
    std::string civilization;
    std::optional<std::string> name;
    std::string layout;
    std::string rebuild;
    std::optional<long long> residents;
};

// Everything that can be judged before the world is copied, in the reference's order.
Checked validate(const World& world, const FoundSettlementRequest& request) {
    if (request.api_version != kSettlementApiVersion) {
        constexpr std::array<long long, 1> supported{kSettlementApiVersion};
        throw RequestError(unsupported_api("api_version", request.api_version, supported));
    }
    if (!world.has_settlements) {
        throw RequestError(missing_block("settlements",
                                         "Founding a settlement needs a generated recipe 3 world with its cities "
                                         "placed; this one carries no settlements block. Cities are placed at stage "
                                         "10, so generate at least that far."));
    }
    if (!request.node) throw RequestError(wrong_type("node", std::monostate{}, "integer"));
    if (*request.node < 0) throw RequestError(out_of_range("node", *request.node, 0, std::nullopt, "grid index"));

    const std::vector<std::string> kind_choices = kinds();
    if (!listed(kind_choices, request.kind)) throw RequestError(invalid_choice("kind", request.kind, kind_choices));
    if (request.kind != "city") {
        if (!world.has_humans) {
            throw RequestError(missing_block("humans", "Founding a " + request.kind +
                                                           " needs a world with a rural layer; hamlets and "
                                                           "fortresses are raised at stage 12, so generate at "
                                                           "least that far."));
        }
        // Both rural blocks key on a parent city by index. A world with none has nothing for
        // the row to answer to, and the two rural planners dereference it directly.
        if (world.settlements.sites.empty()) {
            throw RequestError(missing_block("settlements.sites", "A " + request.kind +
                                                                      " answers to a parent city and this world "
                                                                      "has none placed."));
        }
    }

    const std::vector<std::string> role_choices = roles();
    if (!listed(role_choices, request.role)) throw RequestError(invalid_choice("role", request.role, role_choices));
    if (!request.civilization_id) {
        throw RequestError(wrong_type("civilization_id", std::monostate{}, "string"));
    }
    if (request.name && blank(*request.name)) throw RequestError(wrong_type("name", *request.name, "string"));
    const std::vector<std::string> layout_choices = layouts();
    if (!listed(layout_choices, request.layout)) {
        throw RequestError(invalid_choice("layout", request.layout, layout_choices));
    }
    // Default "none": it leaves every existing join intact. "humans" gives the new city a
    // hinterland but renumbers the rural layer, a trade the caller should make deliberately.
    const std::vector<std::string> rebuild_choices = rebuilds();
    if (!listed(rebuild_choices, request.rebuild)) {
        throw RequestError(invalid_choice("rebuild", request.rebuild, rebuild_choices));
    }
    if (request.population_estimate && *request.population_estimate < 0) {
        throw RequestError(
            out_of_range("population_estimate", *request.population_estimate, 0, std::nullopt, "residents"));
    }

    Checked out;
    out.node = *request.node;
    out.kind = request.kind;
    out.role = request.role;
    out.civilization = *request.civilization_id;
    out.name = request.name;
    out.layout = request.layout;
    out.rebuild = request.rebuild;
    out.residents = request.population_estimate;
    return out;
}

// Every node a settlement of any kind already stands on, with what stands there. A hamlet and
// a fortress may share a node with nothing, including each other.
std::map<int, std::string> occupied_nodes(const World& world) {
    std::map<int, std::string> held;
    for (const SettlementSite& site : world.settlements.sites) {
        held[site.node] = !site.name.empty() ? site.name : !site.uid.empty() ? site.uid : "a city";
    }
    const auto rural = [&](const std::vector<RuralSite>& rows, const char* noun) {
        for (const RuralSite& row : rows) held[row.node] = !row.uid.empty() ? row.uid : !row.id.empty() ? row.id : noun;
    };
    rural(world.humans.hamlets, "a hamlet");
    rural(world.humans.fortresses, "a fortress");
    // A ruin never renames ground a standing settlement already names.
    for (const Ruin& ruin : world.ruins) held.emplace(ruin.node, !ruin.name.empty() ? ruin.name : "a ruin");
    return held;
}

struct Ground {
    std::shared_ptr<const SphereGrid> grid;
    int x = 0;
    int z = 0;
};

// Whether this ground will hold a settlement of this civilization. Every refusal past the
// catalogue check is refused_by_world: the request was well formed and the ground was wrong,
// so the remedy is a different node rather than a different value.
Ground legality(const World& world, int node, const std::string& kind, const std::string& civilization) {
    std::vector<std::string> ids;
    for (const std::string_view id : civilization_ids()) ids.emplace_back(id);
    if (!listed(ids, civilization)) throw RequestError(unknown_field(civilization, ids, "civilization"));

    if (world.layers.count("suitability_" + civilization) == 0) {
        std::vector<std::string> present;
        const std::string prefix = "suitability_";
        for (const auto& [name, grid] : world.layers) {
            if (name.rfind(prefix, 0) == 0) present.push_back(name.substr(prefix.size()));
        }
        throw RequestError(refused_by_world(
            "civilization_id", civilization,
            "This world carries no suitability field for " + py_repr(civilization) +
                ", so it was generated without that civilization. Worlds built with an explicit population_profile "
                "carry only that one.",
            present));
    }

    const int size = world.config.size;
    const double radius = world.effective.globe_radius;
    Ground ground;
    ground.grid = sphere_grid(size, radius);
    const SphereGrid& grid = *ground.grid;
    const std::string at = std::to_string(node);
    if (static_cast<std::size_t>(node) >= grid.node_count()) {
        throw RequestError(refused_by_world("node", node, "Node " + at + " is outside this world; it has " +
                                                              std::to_string(grid.node_count()) + " grid points."));
    }
    const int x = grid.point_x[static_cast<std::size_t>(node)];
    const int z = grid.point_z[static_cast<std::size_t>(node)];
    ground.x = x;
    ground.z = z;

    if (layer_or(world, "water_type", x, z, 0.0) != 0.0) {
        throw RequestError(refused_by_world("node", node, "A settlement cannot be founded on water; node " + at +
                                                              " is not dry land."));
    }
    if (static_cast<int>(layer_or(world, "natural_biome", x, z, 0.0)) == kLandIceBiome) {
        throw RequestError(refused_by_world("node", node, "Node " + at +
                                                              " is persistent land ice, which holds no settlement of "
                                                              "any civilization."));
    }

    const CivilizationProfile& profile = *civilization_profile(civilization);
    const double slope = layer_or(world, "slope", x, z, 0.0);
    if (slope >= profile.traits.site_slope_limit) {
        throw RequestError(refused_by_world("node", node, "Node " + at + " has a slope of " + fixed(slope, 1) +
                                                              " degrees and " + civilization +
                                                              " will not settle at or above " +
                                                              format_double(profile.traits.site_slope_limit) + "."));
    }
    // The people's own reading of the ley where the world publishes one, else the plain hazard.
    std::optional<double> hazard = layer_value(world, "magic_risk_" + civilization, x, z);
    if (!hazard) hazard = layer_or(world, "magic_hazard", x, z, 0.0);
    if (*hazard > profile.traits.mutation_limit) {
        throw RequestError(refused_by_world("node", node, "Node " + at + " carries a magical hazard of " +
                                                              fixed(*hazard, 3) + " and " + civilization +
                                                              " will not settle above " +
                                                              format_double(profile.traits.mutation_limit) + "."));
    }

    const std::map<int, std::string> held = occupied_nodes(world);
    if (const auto it = held.find(node); it != held.end()) {
        throw RequestError(refused_by_world("node", node, "Node " + at + " already holds " + it->second + "."));
    }

    const Vec3 here = direction(x, z, size);
    if (kind == "city") {
        // Cities are spaced by the world's own settlement spacing, in list order: the first
        // city inside it refuses, not the nearest. Rural kinds are not -- a hamlet exists to
        // be near its city -- and are held to their own separation below.
        const double spacing = world.effective.settlement_spacing;
        if (spacing != 0.0) {
            for (const SettlementSite& site : world.settlements.sites) {
                const double metres = great_circle_m(radius, here, site.direction);
                if (metres < spacing) {
                    throw RequestError(refused_by_world(
                        "node", node,
                        "Node " + at + " is " + fixed(metres, 0) + " m from " + (!site.name.empty() ? site.name : site.uid) +
                            " and cities are kept at least " + fixed(spacing, 0) + " m apart."));
                }
            }
        }
    } else {
        const double minimum = kind == "hamlet" ? kHamletSpacingM : kFortressSpacingM;
        for (const auto& [other, label] : held) {
            if (other < 0 || static_cast<std::size_t>(other) >= grid.node_count()) continue;
            const Vec3 there = direction(grid.point_x[static_cast<std::size_t>(other)],
                                         grid.point_z[static_cast<std::size_t>(other)], size);
            const double metres = great_circle_m(radius, here, there);
            if (metres < minimum) {
                throw RequestError(refused_by_world("node", node, "Node " + at + " is " + fixed(metres, 0) + " m from " +
                                                                      label + " and a " + kind +
                                                                      " is kept at least " + fixed(minimum, 0) +
                                                                      " m from anything already standing."));
            }
        }
    }
    return ground;
}

// The `city_layout` a player-authored city carries in place of a derived one. It makes no
// building-placement claim, because city_plans is authoritative for that, and the plan the
// generator writes for this city reports `unbuildable` with the reason.
AuthoredCityLayout authored_layout(const std::string& civilization, std::optional<long long> residents) {
    AuthoredCityLayout layout;
    layout.population_profile = civilization;
    layout.population_estimate = residents.value_or(0);
    return layout;
}

// The record, built from the layers that already describe this ground, so a player town
// carries the numbers a generated one would have carried at the same place rather than
// plausible invented ones.
SettlementSite player_city(const World& world, int node, int x, int z, const Checked& asked, int age) {
    const int size = world.config.size;
    SettlementSite site;
    site.id = static_cast<int>(world.settlements.sites.size());
    site.uid = "surface-city-" + std::to_string(age) + "-" + std::to_string(node) + "-" + asked.civilization;
    site.founded_by = std::string(kPlayer);
    site.node = node;
    site.x = x;
    site.z = z;
    site.direction = direction(x, z, size);
    site.population_profile = asked.civilization;
    // The people's parent race. The reference wrote the civilization id here, which names
    // no parent race for most peoples. The stage-16 redraw resolves a heritage from this
    // field and the heritage refuses an unknown parent race; the hero pass never throws, so
    // the whole cast fails instead (measured: 156 heroes to none, status failed). The
    // reference never ran that pass before its next rebuild recomputed the field, so this
    // port's redraw would turn a stale field into a lost cast. Not carried.
    if (const EntityRules* rules = entity_rule(asked.civilization); rules != nullptr) {
        site.parent_race_id = std::string(rules->parent_race_id);
    }
    site.name = asked.name ? *asked.name : std::string("New Settlement");
    site.name_gloss = "founded by the player";
    site.source_culture = asked.civilization + "-player-" + std::to_string(node);
    site.founded_age = age;
    site.founding_turn = 0;
    site.founding_capital = false;
    site.founding_year = 0;  // the reference's None: no founding round placed it
    site.migration_source_node = -1;
    site.migration_distance_m = 0.0;
    site.cultural_branch = false;

    site.height_m = layer_or(world, "height", x, z, 0.0);
    site.slope_degrees = layer_or(world, "slope", x, z, 0.0);
    site.temperature_c = layer_or(world, "temperature", x, z, 0.0);
    site.moisture = layer_or(world, "moisture", x, z, 0.0);
    site.flood_risk = layer_or(world, "flood_risk", x, z, 0.0);
    site.resource_potential = layer_or(world, "resource_potential", x, z, 0.0);
    // The layer publishes -1 where no fresh water is reachable, and -1 is finite, so this
    // reads -1 where a generated city reads none (infinity here). The reference's defect,
    // reproduced: it tested the layer value for finiteness, not for the sentinel.
    const std::optional<double> fresh = layer_value(world, "freshwater_distance", x, z);
    site.freshwater_distance_m =
        fresh && std::isfinite(*fresh) ? *fresh : std::numeric_limits<double>::infinity();
    site.suitability = layer_or(world, "suitability_" + asked.civilization, x, z, 0.0);

    const CityPopulation people = city_population(asked.residents.value_or(0), 1, 0);
    site.population_estimate = people.residents;
    site.urban_population_estimate = people.urban;
    site.rural_population_estimate = people.rural;
    return site;
}

// A hamlet or fortress row in the block that already holds them. The ordinal id stays in the
// space the block has always used, because the plan blocks join on exactly that; the uid
// beside it is the handle that survives an age boundary.
RuralSite player_rural(const World& world, int node, int x, int z, const Checked& asked, int age, int& core_index) {
    const int size = world.config.size;
    const double radius = world.effective.globe_radius;
    const bool hamlet = asked.kind == "hamlet";
    const std::vector<RuralSite>& rows = hamlet ? world.humans.hamlets : world.humans.fortresses;

    // The nearest city answers for it, by great circle, the first on a tie.
    const Vec3 here = direction(x, z, size);
    core_index = -1;
    double core_distance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < world.settlements.sites.size(); ++i) {
        const double metres = great_circle_m(radius, here, world.settlements.sites[i].direction);
        if (metres < core_distance) {
            core_index = static_cast<int>(i);
            core_distance = metres;
        }
    }

    RuralSite row;
    row.id = asked.kind + "-" + std::to_string(rows.size());
    row.uid = asked.kind + "-node-" + std::to_string(node);
    row.kind = asked.kind;
    row.node = node;
    row.x = x;
    row.z = z;
    row.core_id = core_index;
    row.population_profile = asked.civilization;
    for (const CityCore& core : world.humans.cores) {
        if (core.site_id == core_index) {
            row.culture_id = core.culture_id;
            break;
        }
    }
    row.height_m = layer_or(world, "height", x, z, 0.0);
    // No search stood behind this site, so there is no measured approach path: the cost is
    // the great-circle METRES to the city, where a generated row's is a road cost -- the
    // reference's two units in one field, reproduced -- and one node is a path of length
    // one, which both rural planners read as "no support road".
    row.access_cost = std::isfinite(core_distance) ? core_distance : 0.0;
    row.access_nodes = {node};
    row.reason = "Founded by the player.";
    row.name = asked.name.value_or(std::string());
    row.founded_by = std::string(kPlayer);
    row.founded_age = age;
    if (hamlet) {
        row.role = asked.role;
    } else {
        row.protected_route_node = -1;  // the reference's None
    }
    return row;
}

std::map<std::string, int> ordinals(const std::vector<RuralSite>& rows) {
    std::map<std::string, int> out;
    for (const RuralSite& row : rows) out[row.id] = row.node;
    return out;
}

// Ids whose ground changed, appeared or vanished, sorted.
std::vector<std::string> moved_ids(const std::map<std::string, int>& before, const std::map<std::string, int>& after) {
    std::set<std::string> keys;
    for (const auto& [id, node] : before) keys.insert(id);
    for (const auto& [id, node] : after) keys.insert(id);
    std::vector<std::string> out;
    for (const std::string& id : keys) {
        const auto a = before.find(id);
        const auto b = after.find(id);
        const bool same = a != before.end() && b != after.end() && a->second == b->second;
        if (!same) out.push_back(id);
    }
    return out;
}

// Stage 16 over the world as it now stands, as an age advance redraws it. The threat is
// re-read first: the planners read it BEFORE the raiders raise it, and the raid pass adds
// onto whatever threat it finds, so redrawing over a threat the last pass already raised
// would build walls against raiders twice and raise the threat twice.
void redraw_stage_sixteen(World& world) {
    if (world.has_threat_assessments) {
        // Copied out first: the pass replaces the block these are read from.
        const std::string label = world.threat_assessments.evaluated_after;
        const std::optional<int> age = world.threat_assessments.age;
        assess_world_threats(world, label, age);
    }
    world.city_plans = {};
    world.hamlet_plans = {};
    world.castle_plans = {};
    world.has_plans = false;
    world.world_scene = {};
    world.has_world_scene = false;
    add_simulation_complete(world);
}

}  // namespace

World found_settlement(const World& world, const FoundSettlementRequest& request) {
    const auto started = std::chrono::steady_clock::now();
    const Checked asked = validate(world, request);
    const Ground ground = legality(world, asked.node, asked.kind, asked.civilization);

    World out = world;
    const int age = static_cast<int>(out.history.ages.size());

    std::map<std::string, CityClass> before_class;
    for (const SettlementSite& s : out.settlements.sites) before_class[s.uid] = s.city_class;
    const std::map<std::string, int> before_hamlets = ordinals(out.humans.hamlets);
    const std::map<std::string, int> before_forts = ordinals(out.humans.fortresses);

    SettlementFoundingReport report;
    std::optional<std::string> recorded_name;
    std::string rural_id;
    if (asked.kind == "city") {
        SettlementSite site = player_city(out, asked.node, ground.x, ground.z, asked, age);
        if (asked.layout == kPlayer) site.city_layout = authored_layout(asked.civilization, asked.residents);
        report.uid = site.uid;
        recorded_name = site.name;
        out.settlements.sites.push_back(std::move(site));
        // Classification is a rank over the whole list. A caller never sets it; a strong new
        // site can demote an incumbent, and that is reported rather than hidden.
        classify_settlements(out.settlements.sites);
        report.city_class = out.settlements.sites.back().city_class;
    } else {
        int core_index = -1;
        RuralSite row = player_rural(out, asked.node, ground.x, ground.z, asked, age, core_index);
        report.uid = row.uid;
        rural_id = row.id;
        if (!row.name.empty()) recorded_name = row.name;
        // The parent city's core lists what answers to it, and the wars count a city's forts
        // off exactly that list: a row nothing lists is a settlement no block above it knows.
        for (CityCore& core : out.humans.cores) {
            if (core.site_id != core_index) continue;
            (asked.kind == "hamlet" ? core.hamlet_ids : core.fortress_ids).push_back(row.id);
        }
        (asked.kind == "hamlet" ? out.humans.hamlets : out.humans.fortresses).push_back(std::move(row));
    }

    for (const SettlementSite& s : out.settlements.sites) {
        const auto was = before_class.find(s.uid);
        if (was != before_class.end() && was->second != s.city_class) report.reclassified_cities.push_back(s.uid);
    }
    std::sort(report.reclassified_cities.begin(), report.reclassified_cities.end());

    report.version = kSettlementFoundingReportVersion;
    report.node = asked.node;
    report.kind = asked.kind;
    report.layout = asked.layout;
    report.rebuild = asked.rebuild;
    report.nests_rerolled = false;
    if (asked.kind != "city") {
        report.notes.push_back("A " + asked.kind + " is keyed on " + py_repr(report.uid) +
                               ", the node-and-kind handle npc_roster mints. Its " + py_repr(rural_id) +
                               " id is an ordinal that renumbers whenever the rural layer is rebuilt; do not hold it.");
    }
    if (!report.reclassified_cities.empty()) {
        report.notes.push_back("Founding this city changed the suitability rank of " +
                               std::to_string(report.reclassified_cities.size()) +
                               " existing city/cities. city_class is a rank, not a size.");
    }

    // Stage 16 is redrawn below wherever the world carries it; decided now, before anything
    // can change the flag, because the notes depend on it.
    const bool redraw = out.has_plans;
    if (asked.rebuild == "humans") {
        rebuild_hinterland(out);
        report.rural_ids_reassigned.hamlets = moved_ids(before_hamlets, ordinals(out.humans.hamlets));
        report.rural_ids_reassigned.fortresses = moved_ids(before_forts, ordinals(out.humans.fortresses));
        const std::size_t moved = report.rural_ids_reassigned.hamlets.size();
        const std::size_t moved_forts = report.rural_ids_reassigned.fortresses.size();
        if (moved != 0 || moved_forts != 0) {
            report.notes.push_back("The rural layer was rebuilt, so " + std::to_string(moved) + " hamlet and " +
                                   std::to_string(moved_forts) +
                                   " fortress ordinal ids now name different ground. Those ids renumber at every "
                                   "age boundary by design; key on the terrain node instead.");
        }
    } else {
        // Without a redraw the new settlement has no staffed posts either. With one, a player
        // hamlet or keep is planned and staffed like any other -- a player city is not, the
        // player owning its layout -- so the note says only what is still true.
        const bool staffed = redraw && asked.kind != "city";
        report.notes.push_back(std::string("The rural layer was not rebuilt, so this settlement has no hinterland") +
                               (staffed ? " and no roads" : ", no roads and no staffed posts") +
                               " until something rebuilds it. Founding takes a rebuild argument rather than "
                               "rebuilding on its own or marking the world dirty, so the cost is the caller's to "
                               "choose at the call that caused it.");
    }
    report.notes.push_back(
        "The beast book was deliberately left alone: the nest pass draws from one sequential stream keyed on "
        "distance to settled ground, so re-running it here would re-roll nests world-wide. The next age advance "
        "will re-roll them regardless.");
    if (redraw) {
        report.notes.push_back(
            "Stage 16 was redrawn around the new settlement, as an age advance redraws it: every plan and the world "
            "scene, the bands, herds and encounters, the cast and the story web, the rosters and the key locations "
            "were re-derived from the world as it now stands, after the threat assessment was re-read. Placement "
            "draws the bands from one sequential stream, so a band far from this settlement may re-roll: band, "
            "roster and key-location ids held from before this call may not resolve in the world it returns.");
    }

    std::set<std::string> player_cities;
    for (const SettlementSite& s : out.settlements.sites) {
        if (s.founded_by == kPlayer) player_cities.insert(s.uid);
    }
    out.settlements.founded_by_player.assign(player_cities.begin(), player_cities.end());

    SettlementOperation operation;
    operation.settlement_kind = asked.kind;
    operation.node = asked.node;
    operation.civilization_id = asked.civilization;
    operation.uid = report.uid;
    operation.layout = asked.layout;
    operation.rebuild = asked.rebuild;
    operation.name = recorded_name;
    out.history.operations.push_back(std::move(operation));
    out.settlement_founding = std::move(report);

    if (redraw) redraw_stage_sixteen(out);
    dedupe_systems(out.systems);
    dedupe_warnings(out.warnings);
    out.timing_ms["settlement_founding"] =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return out;
}

}  // namespace fwg
