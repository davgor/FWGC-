#include "fwg/habitation.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

#include "fwg/entities.hpp"
#include "fwg/routing.hpp"
#include "fwg/settlement.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

// Rotted ground. Nobody settles it and nothing grows on it, and that is a rule in the
// code rather than in any habitat table -- no authored rule expresses it, so a people
// whose habitat happens to admit the biome would otherwise settle a dead cell.
constexpr int kBlightedBiome = 17;

std::vector<double> nodes_of(const World& world, const SphereGrid& grid, const char* layer,
                             double fallback = 0.0) {
    const auto it = world.layers.find(layer);
    std::vector<double> out(grid.node_count(), fallback);
    if (it == world.layers.end()) return out;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = it->second.at(grid.point_x[i], grid.point_z[i]);
    }
    return out;
}

bool has_layer(const World& world, const std::string& name) {
    return world.layers.find(name) != world.layers.end();
}

}  // namespace

std::vector<SpeciesAssessment> assess_habitation(const World& world, const SphereGrid& grid,
                                                 const std::vector<std::string>& species) {
    const std::size_t count = grid.node_count();
    // The metre-denominated config, never the design one. See the header: the two differ
    // by the world scale, and a people's reaches are absolute metres.
    const Config& cfg = world.effective;
    const bool recipe = cfg.world_recipe != 0;

    if (std::abs(grid.radius - cfg.globe_radius) > 1e-6 * std::max(1.0, cfg.globe_radius)) {
        throw std::invalid_argument(
            "assess_habitation was given a grid of radius " + std::to_string(grid.radius) +
            " m but this world is " + std::to_string(cfg.globe_radius) +
            " m across; a design-space grid would compare a people's metre reaches "
            "against design units");
    }

    const std::vector<double> water = nodes_of(world, grid, "water_type");
    const std::vector<double> height = nodes_of(world, grid, "height");
    const std::vector<double> slope = nodes_of(world, grid, "slope");
    const std::vector<double> temperature = nodes_of(world, grid, "temperature");
    const std::vector<double> moisture = nodes_of(world, grid, "moisture");
    const std::vector<double> hazard = nodes_of(world, grid, "magic_hazard");
    const std::vector<double> coastal = nodes_of(world, grid, "coastal_support");
    const std::vector<double> biome = nodes_of(world, grid, "biome");
    const std::vector<double> river = nodes_of(world, grid, "rain_river");
    const std::vector<double> resource = nodes_of(world, grid, "metal_richness");

    const FreshwaterAccess fresh = freshwater_access(world, grid);
    const std::vector<double> flood = flood_risk(world, grid, fresh);
    const LandmassContext context = landmass_context(grid, water);

    // Both habitat gates read the same sample, so build it once per node rather than
    // once per node per people.
    std::vector<HabitatSample> samples(count);
    std::vector<std::set<std::string_view>> admitted(count);
    for (std::size_t i = 0; i < count; ++i) {
        samples[i] = environment_at(world, grid, i, context);
        samples[i].resource = resource[i];
        const auto eligible = eligible_civilizations(samples[i]);
        admitted[i].insert(eligible.begin(), eligible.end());
    }

    std::vector<SpeciesAssessment> out;
    out.reserve(species.size());

    for (const std::string& id : species) {
        const CivilizationProfile* profile = civilization_profile(id);
        const EntityRules* rules = entity_rule(id);
        if (profile == nullptr || rules == nullptr) continue;
        const CivilizationTraits& t = profile->traits;

        // A people that has its own magic risk field is judged against that; otherwise
        // against the world's. The per-people field exists because the same ley pressure
        // is not equally dangerous to everyone.
        const std::string risk_layer = "magic_risk_" + id;
        const std::vector<double> risk =
            recipe && has_layer(world, risk_layer) ? nodes_of(world, grid, risk_layer.c_str()) : hazard;

        // On the full world a people is limited only by what it can itself tolerate. On
        // a surface world the world's own ceiling binds as well, whichever is stricter.
        const double hazard_limit =
            recipe ? t.mutation_limit : std::min(cfg.human_magic_limit, t.mutation_limit);

        SpeciesAssessment a;
        a.id = id;
        a.suitability.assign(count, 0.0);
        a.food_potential.assign(count, 0.0);
        std::vector<char> settles(count, 0);

        for (std::size_t i = 0; i < count; ++i) {
            const int core_biome = static_cast<int>(biome[i]);

            SiteConditions at;
            at.freshwater_distance_m = fresh.distance_m[i];
            at.slope_deg = slope[i];
            at.temperature_c = temperature[i];
            at.moisture = moisture[i];
            at.resource = resource[i];
            at.flood = flood[i];
            at.magic_hazard = risk[i];
            at.biome_opinion = biome_preference(*profile, core_biome, samples[i].variant);

            double score = site_suitability(t, at, water[i] != 0.0);
            if (recipe && water[i] == 0.0) {
                // Two extra terms, and only on the full world. The resource weight is
                // read as a flag as well as a number, so an authored zero adds nothing
                // rather than a zero term -- which is the same number but a different
                // statement about whether the people cares.
                double extra = coastal[i] * rules->settlement.coastal_score_weight;
                if (rules->settlement.resource_score_weight != 0.0) {
                    extra += rules->settlement.resource_score_weight * resource[i];
                }
                score = std::max(0.0, std::min(1.0, score + extra));
            }
            a.suitability[i] = score;

            // Gate one: can this people exist here at all?
            bool habitat = admitted[i].count(profile->id) != 0;
            // Gate two: would it BUILD here? A different question and a different table.
            habitat = habitat && settlement_admits(*rules, cfg.world_recipe, samples[i]);
            if (recipe && core_biome == kBlightedBiome) habitat = false;

            const bool safe = water[i] == 0.0 && risk[i] <= hazard_limit;
            bool suitable = habitat && safe && slope[i] < t.site_slope_limit;
            // Gate three, and only where the people authors one. No gate is not the same
            // as a gate of zero: absent means fresh water does not constrain this people.
            if (rules->settlement.freshwater_reach_multiplier.has_value()) {
                const double reach = t.water_reach * *rules->settlement.freshwater_reach_multiplier;
                suitable = suitable && fresh.distance_m[i] <= reach;
            }
            if (suitable) {
                a.candidates.push_back(static_cast<int>(i));
                settles[i] = 1;
            }

            // Food is gated on the WORK slope, not the site slope: a people will farm
            // ground steeper than it will build on.
            const double distance =
                std::isfinite(fresh.distance_m[i]) ? fresh.distance_m[i] : -1.0;
            const FarmingPotential farm = farming_potential(slope[i], temperature[i], moisture[i],
                                                           flood[i], distance, t.irrigation, t);
            double food = farm.irrigated *
                          biome_food_multiplier(*profile, core_biome, samples[i].variant) *
                          (1.0 - risk[i]);
            if (recipe && core_biome == kBlightedBiome) food = 0.0;

            const bool workable = safe && slope[i] < t.work_slope_limit &&
                                  (suitable || rules->settlement.support_outside_habitat);
            a.food_potential[i] = workable ? food : 0.0;
        }

        // A people allowed to farm outside its own habitat still has to be able to REACH
        // the ground it farms. Without this the hinterland would include every workable
        // cell on the planet, including other continents.
        if (rules->settlement.support_outside_habitat) {
            const RoadCost cost(grid, cfg, water, height, flood, river, &hazard);
            const std::vector<char> reachable =
                reachable_support(grid, a.candidates, cost, cfg.support_reach);
            for (std::size_t i = 0; i < count; ++i) {
                if (!reachable[i]) a.food_potential[i] = 0.0;
            }
        }

        out.push_back(std::move(a));
    }
    return out;
}

}  // namespace fwg
