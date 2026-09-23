#include "fwg/debug_stats.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

#include "fwg/build_stages.hpp"
#include "fwg/content.hpp"
#include "fwg/json.hpp"
#include "fwg/sha256.hpp"
#include "fwg/world.hpp"
#include "serialize_document.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

long long urban_total(const std::vector<SettlementSite>& sites) {
    long long total = 0;
    for (const SettlementSite& s : sites) total += s.urban_population_estimate;
    return total;
}

// A group row, first seen first.
DebugGroup& group(std::vector<DebugGroup>& rows, const std::string& id) {
    for (DebugGroup& row : rows) {
        if (row.id == id) return row;
    }
    rows.push_back({id});
    return rows.back();
}

void count(std::vector<std::pair<std::string, long long>>& rows, const std::string& key) {
    for (auto& [k, v] : rows) {
        if (k == key) {
            ++v;
            return;
        }
    }
    rows.emplace_back(key, 1);
}

// The reference's identity for a founding event: (status, node, population_profile,
// parent_race_id, turn, founding_year). An event that founded nothing has none of node,
// people or year -- its dict simply lacks them -- so those three are optional here.
using EventKey = std::tuple<std::string, std::optional<int>, std::optional<std::string>, std::string, int,
                            std::optional<long long>>;

EventKey event_key(const FoundingEvent& e) {
    std::optional<int> node;
    std::optional<long long> year;
    if (e.node >= 0) {
        // A founding carries its site, and the site carries the year its round fell in.
        node = e.node;
        year = e.founding_year;
    }
    std::optional<std::string> people;
    if (!e.population_profile.empty()) people = e.population_profile;
    return {e.status, node, people, e.parent_race_id, e.turn, year};
}

}  // namespace

void record_settlement_stage(World& world, int stage) {
    if (!world.has_settlements) return;
    std::ostringstream text;
    {
        JsonWriter json(text, /*pretty=*/false);
        serialize_detail::write_settlements(json, world);
    }
    std::string fingerprint = sha256_hex(text.str());
    if (!world.settlement_stages.empty() && world.settlement_stages.back().fingerprint == fingerprint) return;

    SettlementStageRecord record;
    record.stage = stage;
    const auto titles = stage_titles();
    record.title = stage >= 1 && stage <= static_cast<int>(titles.size()) ? std::string(titles[static_cast<std::size_t>(stage - 1)])
                                                                           : "Stage " + std::to_string(stage);
    record.cities = static_cast<long long>(world.settlements.sites.size());
    record.urban_population = urban_total(world.settlements.sites);
    record.founding_events = world.settlements.founding.events;
    record.fingerprint = std::move(fingerprint);
    world.settlement_stages.push_back(std::move(record));
}

DebugStats build_debug(const World& world) {
    DebugStats out;
    const std::vector<SettlementSite> none;
    const std::vector<SettlementSite>& sites = world.has_settlements ? world.settlements.sites : none;

    // --- founding events: every stage that changed the settlements, then the world as it
    // stands. A repeat replaces the earlier record in the earlier record's place.
    std::map<EventKey, std::size_t> seen;
    const auto absorb = [&](const std::vector<FoundingEvent>& events) {
        for (const FoundingEvent& e : events) {
            const EventKey key = event_key(e);
            const auto it = seen.find(key);
            if (it != seen.end()) {
                out.founding_events[it->second] = e;
                continue;
            }
            seen.emplace(key, out.founding_events.size());
            out.founding_events.push_back(e);
        }
    };
    for (const SettlementStageRecord& r : world.settlement_stages) absorb(r.founding_events);
    if (world.has_settlements) absorb(world.settlements.founding.events);

    // --- the diaspora: founded, still standing, and fallen without a successor on the spot.
    std::set<std::pair<int, std::string>> live;
    std::set<std::pair<int, std::string>> dead;
    for (const SettlementSite& s : sites) live.insert({s.node, s.population_profile});
    for (const Ruin& r : world.ruins) dead.insert({r.node, r.population_profile});
    for (const FoundingEvent& e : out.founding_events) {
        count(out.founding_outcomes, e.status);
        if (e.status != "diaspora_founded") continue;
        ++out.diaspora_founded;
        const std::pair<int, std::string> key{e.node, e.population_profile};
        if (live.count(key) != 0) {
            ++out.diaspora_surviving;
        } else if (dead.count(key) != 0) {
            ++out.diaspora_destroyed;
        }
    }

    // --- cities, and the peoples they belong to.
    std::map<std::string, const CityPlan*> plans;
    for (const CityPlan& p : world.city_plans.cities) plans[p.city_uid] = &p;
    const double radius = world.effective.globe_radius;
    for (const SettlementSite& s : sites) {
        const std::string uid = s.uid.empty() ? std::to_string(s.id) : s.uid;
        const auto plan_it = plans.find(uid);
        const CityPlan* plan = world.has_plans && plan_it != plans.end() ? plan_it->second : nullptr;

        const long long population = s.urban_population_estimate;
        for (auto* rows : {&out.parent_races, &out.civilizations}) {
            DebugGroup& row = group(*rows, rows == &out.parent_races ? s.parent_race_id : s.population_profile);
            ++row.cities;
            row.urban_population += population;
            if (plan != nullptr) {
                row.workers += plan->stats.workers;
                row.beds += plan->stats.worker_beds;
            }
        }

        DebugCity city;
        city.uid = uid;
        city.name = s.name;
        city.civilization = s.population_profile;
        city.parent_race = s.parent_race_id;
        city.urban_population_estimate = population;
        for (const SettlementSite& other : sites) {
            if (&other == &s) continue;
            const double dot = s.direction.x * other.direction.x + s.direction.y * other.direction.y +
                               s.direction.z * other.direction.z;
            const double distance = radius * std::acos(std::max(-1.0, std::min(1.0, dot)));
            if (!city.nearest_city_m || distance < *city.nearest_city_m) city.nearest_city_m = distance;
        }
        if (plan != nullptr) {
            city.planned = true;
            city.stats = plan->stats;
            city.placement = plan->debug;
            city.unplaced = plan->unplaced;
            for (const RoadConnection& c : plan->road_connections) count(city.junctions, std::string(c.status));
        }
        out.cities.push_back(std::move(city));
    }

    out.ruins = world.ruins;

    for (const SettlementStageRecord& r : world.settlement_stages) {
        out.timeline.push_back({r.stage, r.title, r.cities, r.urban_population});
    }

    // --- ley networks, in school-table order.
    if (world.has_magic) {
        for (const MagicSchool& school : magic_school_table()) {
            const auto it = world.magic.networks.find(std::string(school.name));
            if (it == world.magic.networks.end()) continue;
            const LeyNetwork& net = it->second;
            DebugLeyline row;
            row.id = std::string(school.name);
            row.nodes = static_cast<long long>(net.nodes.size());
            row.lines = static_cast<long long>(net.edges.size());
            if (net.manifested) row.distribution = net.distribution;
            out.leylines.push_back(std::move(row));
        }
    }

    // --- natural biome coverage, weighted by each row's latitude band on the sphere. The
    // duplicate seam column is dropped and the pole bands are clipped at the poles, so the
    // polar rows and the seam do not inflate what they cover.
    const auto grid_it = world.layers.count("natural_biome") != 0 ? world.layers.find("natural_biome")
                                                                  : world.layers.find("biome");
    if (grid_it != world.layers.end()) {
        const Grid& grid = grid_it->second;
        const int n = grid.size();
        std::map<int, double> coverage;
        for (int z = 0; z < n && n > 1; ++z) {
            const double lo = std::max(-kPi / 2.0, kPi / 2.0 - kPi * (z + 0.5) / (n - 1));
            const double hi = std::min(kPi / 2.0, kPi / 2.0 - kPi * (z - 0.5) / (n - 1));
            const double band = std::sin(hi) - std::sin(lo);
            for (int x = 0; x < n - 1; ++x) coverage[static_cast<int>(grid.at(x, z))] += band;
        }
        double total = 0.0;
        for (const auto& [id, value] : coverage) total += value;
        if (total == 0.0) total = 1.0;
        for (const auto& [id, value] : coverage) {
            const NaturalBiome* biome = world.has_terrain ? natural_biome(id) : nullptr;
            out.biome_coverage.push_back(
                {id, biome != nullptr ? std::string(biome->name) : std::to_string(id), 100.0 * value / total});
        }
    }
    return out;
}

}  // namespace fwg
