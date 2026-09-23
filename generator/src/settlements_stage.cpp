#include "fwg/settlements_stage.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>

#include "fwg/entities.hpp"
#include "fwg/heritage.hpp"
#include "fwg/habitation.hpp"
#include "fwg/routing.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

// Which peoples compete for this world's ground. "mixed" means every entity at once;
// anything else means that one people has the planet to itself.
std::vector<std::string> contenders(const Config& cfg) {
    std::vector<std::string> out;
    if (cfg.population_profile == "mixed") {
        for (const auto id : civilization_ids()) out.emplace_back(id);
        return out;
    }
    out.push_back(cfg.population_profile);
    return out;
}

double layer_at(const World& world, const char* name, int x, int z) {
    const auto it = world.layers.find(name);
    return it != world.layers.end() ? it->second.at(x, z) : 0.0;
}

}  // namespace

void add_settlements(World& world, const SettlementRebuild* rebuild) {
    const Config& cfg = world.effective;
    const auto held = sphere_grid(cfg.size, cfg.globe_radius);
    const SphereGrid& grid = *held;

    const std::vector<std::string> species = contenders(cfg);
    const std::vector<SpeciesAssessment> assessed = assess_habitation(world, grid, species);

    SettlementReport report;

    // --- what the ground can feed ------------------------------------------
    std::vector<std::vector<double>> potentials;
    potentials.reserve(assessed.size());
    for (const SpeciesAssessment& a : assessed) potentials.push_back(a.food_potential);
    const LifeCapacity life = life_capacity(grid.areas, potentials);
    report.budget.world_capacity = life.total;

    // --- what each people would ask for ------------------------------------
    std::map<std::string, long long> quotas;
    for (std::size_t i = 0; i < assessed.size(); ++i) {
        const SpeciesAssessment& a = assessed[i];
        const CivilizationProfile* profile = civilization_profile(a.id);
        if (profile == nullptr) continue;

        // Habitat weighted by how good it is, not merely how much there is: a people
        // with a continent of barely-tolerable ground is not owed a continent of cities.
        double weighted_km2 = 0.0;
        for (const int node : a.candidates) {
            const auto n = static_cast<std::size_t>(node);
            weighted_km2 += grid.areas[n] * a.suitability[n];
        }
        weighted_km2 /= 1e6;

        const long long allowance = life.by_people[i];
        report.budget.allowances[a.id] = allowance;
        report.budget.weighted_habitat_km2[a.id] = weighted_km2;

        quotas[a.id] = city_capacity(weighted_km2, allowance, profile->traits.land_per_city_km2,
                                     profile->traits.minimum_founding_residents);
    }

    // --- and what the ground allows whoever asks ---------------------------
    // One habitable set across every people, not the sum of theirs: two peoples that
    // both want the same valley are competing for one valley.
    std::set<int> habitable;
    for (const SpeciesAssessment& a : assessed) {
        habitable.insert(a.candidates.begin(), a.candidates.end());
    }
    double habitable_km2 = 0.0;
    for (const int node : habitable) habitable_km2 += grid.areas[static_cast<std::size_t>(node)] / 1e6;

    const double cell_spacing_m = 2.0 * kPi * cfg.globe_radius / (cfg.size - 1);
    report.budget.ceiling = city_ceiling(habitable_km2, cfg.settlement_spacing, cell_spacing_m);
    report.budget.city_limit = city_limit(report.budget.ceiling.packing, cfg, 24);
    apply_city_ceiling(quotas, report.budget.city_limit);
    report.budget.requested_cities = quotas;

    // --- who settles where -------------------------------------------------
    FoundingRequest request;
    for (const ParentRace& r : parent_races()) request.parents.push_back(r);
    for (const SpeciesAssessment& a : assessed) {
        const EntityRules* rules = entity_rule(a.id);
        const CivilizationProfile* profile = civilization_profile(a.id);
        if (rules == nullptr || profile == nullptr) continue;
        request.owners[a.id] = std::string(rules->parent_race_id);
        request.candidates[a.id] = a.candidates;
        request.scores[a.id] = a.suitability;
        request.minimum_residents[a.id] =
            static_cast<long long>(profile->traits.minimum_founding_residents);
    }
    for (const auto& [id, allowance] : report.budget.allowances) {
        request.food_allowances[id] = allowance;
    }
    request.quotas = quotas;
    request.spacing = cfg.settlement_spacing;
    request.world_reach = kPi * cfg.globe_radius;  // half the way round: the longest useful hop
    request.city_limit = report.budget.city_limit;
    request.rules = authored_founding_rules();
    request.rules.magic_enabled = cfg.magic_enabled != 0;

    // A rebuild founds AROUND what stood through the age turn. The survivors go in as
    // already-placed sites, so spacing and quotas count them; the ruins are ground nobody
    // builds on; and the clock resumes a round after the last age's founding ended rather
    // than restarting at year zero on top of it.
    if (rebuild != nullptr) {
        for (const SettlementSite& s : rebuild->survivors) {
            FoundedSite kept;
            kept.node = s.node;
            kept.population_profile = s.population_profile;
            kept.parent_race_id = s.parent_race_id;
            kept.founding_turn = s.founding_turn;
            kept.founding_capital = s.founding_capital;
            kept.founding_year = s.founding_year;
            kept.migration_source_node = s.migration_source_node;
            kept.source_civilization_id = s.source_civilization_id;
            kept.migration_distance_m = s.migration_distance_m;
            kept.cultural_branch = s.cultural_branch;
            kept.diaspora = s.diaspora;
            kept.diaspora_reason = s.diaspora_reason;
            request.survivors.push_back(std::move(kept));
        }
        request.forbidden = rebuild->forbidden_nodes;
        request.diaspora_bonus_used = rebuild->previous.diaspora_bonus_used;
        request.used_civilizations = rebuild->previous.used_civilizations;
        request.rules.start_year = rebuild->previous.end_year + request.rules.years_per_round;
    }

    // Great-circle distance between two nodes, in metres.
    const auto distance = [&](int a, int b) {
        const Vec3 p = direction(grid.point_x[static_cast<std::size_t>(a)],
                                 grid.point_z[static_cast<std::size_t>(a)], cfg.size);
        const Vec3 q = direction(grid.point_x[static_cast<std::size_t>(b)],
                                 grid.point_z[static_cast<std::size_t>(b)], cfg.size);
        const double dot = std::max(-1.0, std::min(1.0, p.x * q.x + p.y * q.y + p.z * q.z));
        return cfg.globe_radius * std::acos(dot);
    };

    const FoundingResult founded = found_cities(cfg.seed, request, distance);
    report.founding = founded.report;
    // [document: population_budget] What the reference publishes as requested_cities is
    // the quotas founding ended on, not the ones it was handed: it rebinds `quotas` to the
    // founding report's effective quotas before writing the budget.
    report.budget.requested_cities = founded.report.effective_quotas;

    // --- the records -------------------------------------------------------
    std::map<std::string, const SpeciesAssessment*> by_id;
    for (const SpeciesAssessment& a : assessed) by_id[a.id] = &a;

    std::vector<CityClassInput> classifiable;
    classifiable.reserve(founded.sites.size());
    for (const FoundedSite& s : founded.sites) {
        const auto n = static_cast<std::size_t>(s.node);
        const auto it = by_id.find(s.population_profile);
        classifiable.push_back({s.node, s.population_profile,
                                it != by_id.end() ? it->second->suitability[n] : 0.0,
                                s.founding_capital});
    }
    const std::vector<CityClass> classes = classify_cities(classifiable, registry_defaults().medium_suitability_min);

    const FreshwaterAccess fresh = freshwater_access(world, grid);
    const std::vector<double> flood = flood_risk(world, grid, fresh);

    // Each people names its own cities, in its own tongue. Resolving a heritage is not
    // cheap and every city of a people shares one, so resolve once per people rather
    // than once per city.
    const heritage::Lexicon& lexicon = heritage::lexicon();
    std::map<std::string, heritage::Resolved> heritages;
    for (const SpeciesAssessment& a : assessed) {
        const EntityRules* rules = entity_rule(a.id);
        if (rules == nullptr) continue;
        heritages.emplace(a.id, heritage::resolve(a.id, rules->parent_race_id));
    }

    // Population is split per people, in founding order, so the oldest city carries the
    // odd resident. Count each people's cities first: the split needs the total.
    std::map<std::string, long long> city_counts;
    for (const FoundedSite& s : founded.sites) ++city_counts[s.population_profile];
    std::map<std::string, long long> seen_so_far;

    for (std::size_t i = 0; i < founded.sites.size(); ++i) {
        const FoundedSite& s = founded.sites[i];
        const auto n = static_cast<std::size_t>(s.node);
        const int x = grid.point_x[n];
        const int z = grid.point_z[n];

        SettlementSite site;
        site.id = static_cast<int>(i);
        // No uid here: remember_cities assigns one to a NEW city and a survivor carries its
        // own. Minting it at founding would stamp every city with the current age,
        // including one that has stood since the first.
        site.node = s.node;
        site.x = x;
        site.z = z;
        site.direction = direction(x, z, cfg.size);
        site.population_profile = s.population_profile;
        site.parent_race_id = s.parent_race_id;

        // Named on its NODE rather than its position in the list, so a city keeps its
        // name when an age advance renumbers the settlements around it.
        const auto heritage_it = heritages.find(s.population_profile);
        if (heritage_it != heritages.end()) {
            Rng naming = Rng::child(cfg.seed, "settlement-name-" + std::to_string(s.node));
            auto [name, gloss] = heritage::settlement_name(heritage_it->second, lexicon, naming);
            site.name = std::move(name);
            site.name_gloss = std::move(gloss);
        }

        site.city_class = classes[i];
        site.classification_reason =
            classes[i] == CityClass::Capital
                ? "Founding capital retained; otherwise highest suitability, node breaks ties"
                : classes[i] == CityClass::Medium ? "Suitability at or above the medium threshold"
                                                  : "Suitability below the medium threshold";
        site.founding_capital = s.founding_capital;
        site.founding_turn = s.founding_turn;
        site.founding_year = s.founding_year;

        const auto it = by_id.find(s.population_profile);
        site.suitability = it != by_id.end() ? it->second->suitability[n] : 0.0;
        site.height_m = layer_at(world, "height", x, z);
        site.slope_degrees = layer_at(world, "slope", x, z);
        site.temperature_c = layer_at(world, "temperature", x, z);
        site.moisture = layer_at(world, "moisture", x, z);
        site.resource_potential = layer_at(world, "metal_richness", x, z);
        site.flood_risk = flood[n];
        site.freshwater_distance_m = fresh.distance_m[n];

        const long long index = seen_so_far[s.population_profile]++;
        const auto allowance = report.budget.allowances.find(s.population_profile);
        const CityPopulation people =
            city_population(allowance != report.budget.allowances.end() ? allowance->second : 0,
                            city_counts[s.population_profile], index);
        site.population_estimate = people.residents;
        site.urban_population_estimate = people.urban;
        site.rural_population_estimate = people.rural;

        site.migration_source_node = s.migration_source_node;
        site.source_civilization_id = s.source_civilization_id;
        site.migration_distance_m = s.migration_distance_m;
        site.cultural_branch = s.cultural_branch;
        site.diaspora = s.diaspora;
        site.diaspora_reason = s.diaspora_reason;

        // A city that stood through the turn is the SAME city, not a new one on the same
        // ground: it keeps its identity, the age and culture that founded it, who
        // founded it, the refugees it took in and the wars it fought. Matched on node AND people, because a
        // different people founding on a fallen city's node is a new city there.
        if (rebuild != nullptr) {
            for (const SettlementSite& old : rebuild->survivors) {
                if (old.node != site.node || old.population_profile != site.population_profile) continue;
                site.uid = old.uid;
                site.name = old.name;
                site.name_gloss = old.name_gloss;
                site.founded_age = old.founded_age;
                site.source_culture = old.source_culture;
                site.founded_by = old.founded_by;
                site.absorbed_bands = old.absorbed_bands;
                site.absorbed_refugees = old.absorbed_refugees;
                site.war_history = old.war_history;
                break;
            }
        }
        // Refugees walked in from a town that fell; the split above is derived from the
        // ground this city farms and cannot know about them. Added after it, so the split
        // stays a pure function of habitat and the arrivals stay auditable on their own.
        if (site.absorbed_refugees > 0) {
            const CityPopulation with = city_population(site.population_estimate, 1, 0, site.absorbed_refugees);
            site.population_estimate = with.residents;
            site.urban_population_estimate = with.urban;
            site.rural_population_estimate = with.rural;
        }

        report.sites.push_back(std::move(site));
    }

    // [document: population_budget] The rest of the reference's budget block: each people's
    // share of the capacity, its founding minimum, how many cities it holds and how much of
    // the capacity the cities took. After the sites, because `allocated` counts the refugees
    // a survivor took in, which were added above after the capacity split -- as the
    // reference counts them.
    for (const SpeciesAssessment& a : assessed) {
        const auto allowance = report.budget.allowances.find(a.id);
        const long long own = allowance != report.budget.allowances.end() ? allowance->second : 0;
        report.budget.shares[a.id] = report.budget.world_capacity > 0
                                         ? static_cast<double>(own) / static_cast<double>(report.budget.world_capacity)
                                         : 0.0;
        if (const CivilizationProfile* profile = civilization_profile(a.id)) {
            report.budget.minimum_city_region_residents[a.id] =
                static_cast<long long>(profile->traits.minimum_founding_residents);
        }
        report.budget.placed_cities[a.id] = 0;
    }
    for (const SettlementSite& s : report.sites) {
        ++report.budget.placed_cities[s.population_profile];
        report.budget.allocated += s.population_estimate;
    }
    report.budget.unallocated = report.budget.world_capacity - report.budget.allocated;

    // Each people's opinion of the whole world, published so a reader can see the field
    // a siting decision came out of rather than only the decision.
    for (const SpeciesAssessment& a : assessed) {
        world.layers["suitability_" + a.id] = grid.to_grid(a.suitability);
        world.layers["life_capacity_" + a.id] = grid.to_grid(a.food_potential);
    }

    // The four generic fields later stages read by name. Colleges, witch huts and key
    // locations all look these up, and a missing layer does not fail -- it reads as zero,
    // which silently admits every cell to a test that was meant to refuse most of them.
    //
    // `suitability` is the FIRST contender's field, as the reference has it, not a blend
    // and not the people holding the most ground. On the shipped registry the first entry
    // is human_maritime, which settles no world at shipped widths (docs/scale-defects.md)
    // -- but its opinion of the ground is a real field whether or not it can act on it,
    // and matching the reference's choice keeps every consumer of the name comparable.
    if (!assessed.empty()) world.layers["suitability"] = grid.to_grid(assessed.front().suitability);
    world.layers["flood_risk"] = grid.to_grid(flood);
    world.layers["resource_potential"] = world.layers.count("metal_richness") != 0
                                             ? world.layers.at("metal_richness")
                                             : Grid(cfg.size);
    {
        // Unreachable water publishes -1, not infinity: a grid is read cell by cell by
        // consumers that compare against reaches, and -1 is the reference's own spelling
        // of "none" in this layer.
        std::vector<double> fresh_m(fresh.distance_m.size());
        for (std::size_t i = 0; i < fresh_m.size(); ++i) {
            fresh_m[i] = std::isfinite(fresh.distance_m[i]) ? fresh.distance_m[i] : -1.0;
        }
        world.layers["freshwater_distance"] = grid.to_grid(fresh_m);
    }

    world.settlements = std::move(report);
    world.has_settlements = true;
    world.systems.push_back("settlements");
    remember_cities(world, rebuild != nullptr ? rebuild->age : 0);
}

void remember_cities(World& world, int age) {
    std::map<int, const CityCore*> cores;
    if (world.has_humans) {
        for (const CityCore& c : world.humans.cores) cores[c.site_id] = &c;
    }
    for (SettlementSite& city : world.settlements.sites) {
        if (city.uid.empty()) {
            // A new city in a later age takes its age into its name. Names are keyed on the
            // node, so a city founded where one fell would otherwise carry exactly the
            // fallen city's name -- the suffix is what tells a reader they are two.
            if (age > 0) city.name += " (Age " + std::to_string(age) + ")";
            city.uid = "surface-city-" + std::to_string(age) + "-" + std::to_string(city.node) + "-" +
                       city.population_profile;
            city.founded_age = age;
        }
        // A city founded THIS age takes its culture from its own core, once the hinterland
        // has placed the cores; until then, and for a city with none, a name that at least
        // says who founded it and where. A survivor keeps the culture it was founded with.
        if (city.founded_age == age) {
            if (const auto it = cores.find(city.id); it != cores.end()) city.source_culture = it->second->culture_id;
        }
        if (city.source_culture.empty()) {
            city.source_culture = city.population_profile + "-founders-" + std::to_string(city.node);
        }
    }
}

}  // namespace fwg
