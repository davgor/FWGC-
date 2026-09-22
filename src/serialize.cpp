#include "fwg/serialize.hpp"

#include <cmath>
#include <ostream>

#include "fwg/json.hpp"

namespace fwg {
namespace {

void write_config(JsonWriter& json, const Config& cfg) {
    json.begin_object();
    json.field("seed", static_cast<long long>(cfg.seed));
    json.field("size", cfg.size);
    json.field("phase", cfg.phase);
    json.key("world_size");
    json.value(std::string_view(to_string(cfg.world_size)));
    // Whether that preset is what built the world, and the width that did.
    json.field("world_size_was_used", cfg.world_size_was_used);
    json.field("circumference_m", cfg.circumference_m);
    json.field("globe_radius", cfg.globe_radius);
    json.field("world_scale", cfg.world_scale);
    json.field("tectonic_relief", cfg.tectonic_relief);
    json.field("amplitude", cfg.amplitude);
    json.field("wavelength", cfg.wavelength);
    json.field("octaves", cfg.octaves);
    json.field("ridge", cfg.ridge);
    json.field("sea_level", cfg.sea_level);
    json.field("radius", cfg.radius);
    json.field("plate_count", cfg.plate_count);
    json.field("layout_variation", cfg.layout_variation);
    json.field("detail_variation", cfg.detail_variation);
    json.field("crust_bias", cfg.crust_bias);
    json.field("belt_width", cfg.belt_width);
    json.field("mountain_detail", cfg.mountain_detail);
    json.field("orogeny", cfg.orogeny);
    json.field("mountain_abundance", cfg.options[Option::mountain_abundance]);
    json.field("erosion_passes", cfg.erosion_passes);
    json.field("erosion_strength", cfg.erosion_strength);
    json.field("magic_enabled", cfg.magic_enabled);
    json.field("world_recipe", cfg.world_recipe);
    json.field("auto_parameters", cfg.auto_parameters);
    json.field("population_profile", cfg.population_profile);
    json.field("wind_bearing", cfg.wind_bearing);
    json.field("rain_passes", cfg.rain_passes);
    json.field("rain_strength", cfg.rain_strength);
    json.field("temperature_offset", cfg.temperature_offset);
    json.field("moisture_bias", cfg.moisture_bias);
    json.field("road_max_grade", cfg.road_max_grade);
    json.field("bridge_cost", cfg.bridge_cost);
    json.field("human_magic_limit", cfg.human_magic_limit);
    json.field("stubbornness", cfg.stubbornness);
    json.field("urban_food_demand", cfg.urban_food_demand);
    json.field("human_adaptation", cfg.human_adaptation);
    json.field("settlement_count", cfg.settlement_count);
    json.field("hamlets_per_core", cfg.hamlets_per_core);
    json.field("fortress_count", cfg.fortress_count);
    json.field("college_count", cfg.college_count);
    json.field("settlement_spacing", cfg.settlement_spacing);
    json.field("support_reach", cfg.support_reach);
    json.field("culture_link_cost", cfg.culture_link_cost);
    json.field("river_threshold_km2", cfg.river_threshold_km2);
    json.end_object();
}

}  // namespace

void write_world_json(std::ostream& out, const World& world, bool pretty, bool include_timings) {
    JsonWriter json(out, pretty);
    json.begin_object();

    json.field("schema", std::string_view("fwg.world"));
    json.field("schema_version", kWorldSchemaVersion);
    json.field("generator_version", world.generator_version);
    json.field("completed_stage", world.completed_stage);

    json.key("config");
    write_config(json, world.config);
    json.key("effective_config");
    write_config(json, world.effective);

    json.key("physical_scale");
    json.begin_object();
    json.field("factor", world.config.world_scale);
    json.field("note", std::string_view("config keeps the design inputs; effective_config and every layer are in metres"));
    json.end_object();

    json.key("topology");
    json.begin_object();
    json.field("type", std::string_view("sphere"));
    json.field("radius_m", world.effective.globe_radius);
    json.key("longitude_deg");
    json.begin_array();
    json.value(-180.0);
    json.value(180.0);
    json.end_array();
    json.key("latitude_deg");
    json.begin_array();
    json.value(90.0);
    json.value(-90.0);
    json.end_array();
    json.field("duplicate_seam", true);
    json.field("tpi_method", std::string_view("8-point geodesic ring"));
    json.end_object();

    json.field("spacing_m", world.spacing_m);
    json.field("effective_tpi_radius_m", world.effective_tpi_radius_m);
    json.field("resolved_octaves", world.resolved_octaves);

    json.key("seeds");
    json.begin_object();
    // As a STRING, not a number. It is 64-bit, and JSON numbers are doubles to most
    // readers -- a JavaScript consumer silently rounds anything above 2^53, and a
    // static_cast<long long> here would go negative above 2^63.
    json.key("layout");
    json.value(std::string_view(std::to_string(world.layout_seed)));
    json.field("crust", static_cast<long long>(world.crust_seed));
    json.field("detail", static_cast<long long>(world.detail_seed));
    json.end_object();

    json.key("plates");
    json.begin_array();
    for (const Plate& plate : world.plates) {
        json.begin_object();
        json.field("id", plate.id);
        json.key("center");
        json.begin_array();
        json.value(plate.center.x);
        json.value(plate.center.y);
        json.value(plate.center.z);
        json.end_array();
        json.key("omega");
        json.begin_array();
        json.value(plate.omega.x);
        json.value(plate.omega.y);
        json.value(plate.omega.z);
        json.end_array();
        json.end_object();
    }
    json.end_array();
    json.field("motion_units", std::string_view("dimensionless angular velocity"));

    if (world.config.phase >= 2) {
        json.key("area");
        json.begin_object();
        json.field("land_km2", world.area.land_km2);
        json.field("submerged_km2", world.area.submerged_km2);
        json.field("total_km2", world.area.total_km2);
        json.field("land_fraction", world.area.land_fraction);
        json.field("largest_sample_km2", world.area.largest_sample_km2);
        json.field("method", std::string_view(
                                 "latitude-weighted nodal footprint; duplicate longitude excluded; height > sea level"));
        json.field("meaning", std::string_view(
                                  "reference-sphere footprint, not slope surface area, connected land, or walkability"));
        json.end_object();
    }

    if (world.has_sediment_budget) {
        json.key("sediment_budget");
        json.begin_object();
        json.field("eroded_m3", world.sediment.eroded_m3);
        json.field("deposited_m3", world.sediment.deposited_m3);
        json.field("stored_sediment_m3", world.sediment.stored_sediment_m3);
        json.field("balance_error_m3", world.sediment.balance_error_m3);
        json.field("passes", world.sediment.passes);
        json.field("method",
                   std::string_view("downhill stream-power proxy; closed depressions and submerged cells retain "
                                    "sediment; no lake overflow"));
        json.end_object();
    }

    if (world.has_water) {
        json.key("water");
        json.begin_object();
        json.field("version", world.water.version);
        json.field("ocean_km2", world.water.ocean_km2);
        json.field("lake_km2", world.water.lake_km2);
        json.field("dry_km2", world.water.dry_km2);
        json.field("lake_capacity_m3", world.water.lake_capacity_m3);
        json.field("has_ocean", world.water.has_ocean);
        json.field("river_segment_count", static_cast<long long>(world.water.river_segments.size()));
        json.key("receivers");
        json.begin_array();
        for (const int r : world.water.receivers) json.value(static_cast<long long>(r));
        json.end_array();
        json.key("river_segments");
        json.begin_array();
        for (const auto& seg : world.water.river_segments) {
            json.begin_array();
            json.value(seg[0]);
            json.value(seg[1]);
            json.end_array();
        }
        json.end_array();
        json.field("method", std::string_view(
            "Largest connected below-sea component is the ocean; inland depressions are filled to their spill "
            "levels assuming sufficient water. No rainfall budget, no time-dependent filling, no evaporation, "
            "no river width."));
        json.field("routing", std::string_view(
            "Receivers index unique spherical nodes; -1 is an ocean outlet or the global minimum in an oceanless "
            "world. Catchment assumes uniform unit runoff."));
        json.end_object();
    }

    if (world.has_climate) {
        json.key("climate");
        json.begin_object();
        json.field("version", world.climate.version);
        json.field("passes", world.climate.passes);
        json.field("residual", world.climate.residual);
        json.field("transport_step_m", world.climate.transport_step_m);
        json.field("runoff_input_m2_equivalent", world.climate.runoff_input_m2_equivalent);
        json.field("runoff_outlets_m2_equivalent", world.climate.runoff_outlets_m2_equivalent);
        json.field("river_segment_count", static_cast<long long>(world.climate.river_segments.size()));
        json.key("river_segments");
        json.begin_array();
        for (const auto& seg : world.climate.river_segments) {
            json.begin_array();
            json.value(seg[0]);
            json.value(seg[1]);
            json.end_array();
        }
        json.end_array();
        json.key("wind_arrows");
        json.begin_array();
        for (const WindArrow& a : world.climate.wind_arrows) {
            json.begin_object();
            json.field("x", a.x);
            json.field("z", a.z);
            json.key("direction");
            json.begin_array();
            json.value(a.at.x);
            json.value(a.at.y);
            json.value(a.at.z);
            json.end_array();
            json.key("wind");
            json.begin_array();
            json.value(a.wind.x);
            json.value(a.wind.y);
            json.value(a.wind.z);
            json.end_array();
            json.end_object();
        }
        json.end_array();
        json.field("method", std::string_view(
            "Fixed compass-bearing wind transports humidity from equilibrium ocean and lake surfaces. Rising air "
            "loses extra moisture as rainfall, drying the lee. Relative proxies, not mm/year or discharge."));
        json.field("convergence", std::string_view(
            "Fixed iteration count; residual is the max humidity change on the final pass. No convergence "
            "guarantee."));
        json.end_object();
    }

    if (world.has_terrain) {
        json.key("terrain");
        json.begin_object();
        json.field("version", world.terrain.version);
        json.field("climate_seed", static_cast<long long>(world.terrain.climate_seed));
        json.key("landforms");
        json.begin_array();
        for (int i = 0; i < 4; ++i) json.value(std::string_view(landform_name(i)));
        json.end_array();
        json.key("natural_biomes");
        json.begin_array();
        for (const NaturalBiome& b : natural_biomes()) {
            json.begin_object();
            json.field("id", b.id);
            json.field("core", b.core);
            json.field("name", b.name);
            json.field("asset_id", b.asset_id);
            json.key("color");
            json.begin_array();
            for (const int c : b.color) json.value(c);
            json.end_array();
            json.field("reachable", b.reachable);
            json.end_object();
        }
        json.end_array();
        json.key("features");
        json.begin_array();
        for (const TerrainFeature& f : world.terrain.features) {
            json.begin_object();
            json.field("kind", f.kind);
            json.field("label", f.label);
            json.field("x", f.x);
            json.field("z", f.z);
            json.field("height_m", f.height_m);
            json.key("direction");
            json.begin_array();
            json.value(f.direction.x);
            json.value(f.direction.y);
            json.value(f.direction.z);
            json.end_array();
            json.end_object();
        }
        json.end_array();
        json.field("method", world.terrain.method);
        json.field("icons", std::string_view(
            "Sparse representative regions and prominent ridges; not unique geological objects."));
        json.end_object();
    }

    if (world.has_magic) {
        json.key("magic");
        json.begin_object();
        json.field("version", world.magic.version);
        json.field("enabled", world.magic.enabled);
        json.field("mutation_threshold", world.magic.mutation_threshold);
        json.field("dominance_margin", world.magic.dominance_margin);
        json.key("school_order");
        json.begin_array();
        for (const MagicSchool& s : magic_school_table()) json.value(s.name);
        json.end_array();
        json.key("networks");
        json.begin_object();
        for (const MagicSchool& school : magic_school_table()) {
            const LeyNetwork& net = world.magic.networks.at(std::string(school.name));
            json.key(net.name);
            json.begin_object();
            json.field("group", net.group);
            json.field("descriptors", net.descriptors);
            json.field("seed", static_cast<long long>(net.seed));
            json.field("strength", net.strength);
            json.field("width_m", net.width_m);
            json.field("instability", net.instability);
            json.field("manifested", net.manifested);
            if (net.has_surge) json.field("surged_strength", net.surged_strength);
            json.field("hidden", school.hidden);
            json.key("color");
            json.begin_array();
            for (const int c : net.color) json.value(c);
            json.end_array();
            json.key("nodes");
            json.begin_array();
            for (const LeyNode& node : net.nodes) {
                json.begin_object();
                json.field("id", node.id);
                json.field("intensity", node.intensity);
                json.key("direction");
                json.begin_array();
                json.value(node.direction.x);
                json.value(node.direction.y);
                json.value(node.direction.z);
                json.end_array();
                json.end_object();
            }
            json.end_array();
            json.key("edges");
            json.begin_array();
            for (const LeyEdge& e : net.edges) {
                json.begin_object();
                json.field("id", e.id);
                json.field("from", e.from);
                json.field("to", e.to);
                json.field("intensity", e.intensity);
                json.end_object();
            }
            json.end_array();
            if (net.manifested) {
                json.key("distribution");
                json.begin_object();
                json.field("version", net.distribution.version);
                json.field("model", std::string_view("landscape_alignments"));
                json.field("clusters", net.distribution.clusters);
                json.field("spread", net.distribution.spread);
                json.field("outlier_fraction", net.distribution.outlier_fraction);
                json.field("extra_link_probability", net.distribution.extra_link_probability);
                json.key("aligned_nodes");
                json.begin_array();
                for (const int a : net.distribution.aligned_nodes) json.value(a);
                json.end_array();
                json.end_object();
            }
            json.end_object();
        }
        json.end_object();
        json.key("compatibility_fields");
        json.begin_object();
        json.field("ley_holy", std::string_view("radiant"));
        json.field("ley_primordial", std::string_view("maximum of fire, water, earth, air"));
        json.end_object();
        json.field("method", std::string_view(
            "Eight known schools as independently seeded sacred alignments with uneven clusters, scattered "
            "outliers and variable connectivity, plus four hidden schools that generation never raises: their "
            "occurrence is locked at zero and only the corruption API places a node in one. Node and line "
            "intensities are editable and overlapping raw potency is preserved. Mutation requires potency at "
            "or above the threshold and a lead over the next strongest school of at least the margin."));
        json.end_object();
    }

    if (world.has_epoch) {
        json.key("geological_history");
        json.begin_object();
        json.field("version", world.epoch.version);
        json.field("elapsed_million_years", world.epoch.elapsed_million_years);
        json.field("angular_duration", world.epoch.angular_duration);
        for (const auto& [key, plates] :
             {std::pair<const char*, const std::vector<Plate>*>{"plates_before", &world.epoch.plates_before},
              std::pair<const char*, const std::vector<Plate>*>{"plates_after", &world.epoch.plates_after}}) {
            json.key(key);
            json.begin_array();
            for (const Plate& plate : *plates) {
                json.begin_object();
                json.field("id", plate.id);
                json.key("center");
                json.begin_array();
                json.value(plate.center.x);
                json.value(plate.center.y);
                json.value(plate.center.z);
                json.end_array();
                json.key("omega");
                json.begin_array();
                json.value(plate.omega.x);
                json.value(plate.omega.y);
                json.value(plate.omega.z);
                json.end_array();
                json.end_object();
            }
            json.end_array();
        }
        json.field("method", std::string_view(
            "50-million-year artistic epoch: rotate the original plate centres, backtrace crust, blend "
            "transported relief with new boundary uplift. Angular speeds are dimensionless, not calibrated "
            "geodynamics."));
        json.end_object();
    }

    if (world.has_astrology) {
        const auto schools = magic_school_table();
        const auto tide_object = [&](JsonWriter& w, const std::array<double, kKnownSchools>& t) {
            w.begin_object();
            for (int s = 0; s < kKnownSchools; ++s) {
                w.field(schools[static_cast<std::size_t>(s)].name, t[static_cast<std::size_t>(s)]);
            }
            w.end_object();
        };
        const auto day_object = [&](JsonWriter& w, const DayState& d) {
            w.begin_object();
            w.field("day", static_cast<long long>(d.day));
            w.field("phase_fraction", d.phase_fraction);
            w.field("elongation_degrees", d.elongation_degrees);
            w.field("illumination", d.illumination);
            w.field("moonrise_hour", d.moonrise_hour);
            w.field("spin_fraction", d.spin_fraction);
            w.field("nod_degrees", d.nod_degrees);
            w.field("leaning", d.leaning);
            w.key("tide");
            tide_object(w, d.tide);
            w.end_object();
        };

        json.key("astrology");
        json.begin_object();
        json.field("version", 1);
        json.key("calendar");
        json.begin_object();
        json.field("hours_per_day", kHoursPerDay);
        json.field("days_per_month", kDaysPerMonth);
        json.field("months_per_year", kMonthsPerYear);
        json.field("days_per_year", kDaysPerYear);
        json.field("epoch", std::string_view("day 0 is founding year 0; hour 0 is sunrise"));
        json.end_object();
        json.key("moon");
        json.begin_object();
        json.key("periods");
        json.begin_object();
        json.field("synodic", world.moon.synodic_days);
        json.field("spin", world.moon.spin_days);
        json.field("nod", world.moon.nod_days);
        json.end_object();
        json.key("offsets");
        json.begin_object();
        json.field("synodic", world.moon.synodic_offset);
        json.field("spin", world.moon.spin_offset);
        json.field("nod", world.moon.nod_offset);
        json.end_object();
        json.field("tilt_max_degrees", world.moon.tilt_max_degrees);
        json.field("great_year_days", static_cast<long long>(world.moon.great_year_days));
        json.key("regions");
        json.begin_object();
        for (int s = 0; s < kKnownSchools; ++s) {
            json.key(schools[static_cast<std::size_t>(s)].name);
            const Vec3 r = school_region(s);
            json.begin_array();
            json.value(r.x);
            json.value(r.y);
            json.value(r.z);
            json.end_array();
        }
        json.end_object();
        json.end_object();
        json.field("method", std::string_view(
            "One moon, not tidally locked. Its still hemisphere carries radiant, water, earth and umbral; its "
            "restless hemisphere weave, fire, air and infernal, one school per quarter. Seeded integer-day "
            "phase, spin and nod cycles prefer coprime periods, so the great year is their least common "
            "multiple. A school surges when its quarter both faces the world and is lit."));
        json.field("limits", std::string_view(
            "Artistic astronomy: one body reduced to phase, rise hour, spin and lean. No altitude, no latitude "
            "dependence, no solar eclipses, no water tides."));
        json.end_object();

        json.key("lunar_almanac");
        json.begin_object();
        json.field("version", 1);
        json.field("reported_year", world.lunar_almanac.reported_year);
        json.field("reported_year_source", world.lunar_almanac.reported_year_source);
        json.field("next_grand_alignment_day",
                   static_cast<long long>(world.lunar_almanac.next_grand_alignment_day));
        json.key("now");
        day_object(json, world.lunar_almanac.now);
        json.key("months");
        json.begin_array();
        for (const AlmanacMonth& m : world.lunar_almanac.months) {
            json.begin_object();
            json.field("first_day", static_cast<long long>(m.first_day));
            json.key("state");
            day_object(json, m.state);
            json.key("mean_tide");
            tide_object(json, m.mean_tide);
            json.end_object();
        }
        json.end_array();
        json.key("events");
        json.begin_array();
        for (const AlmanacEvent& e : world.lunar_almanac.events) {
            json.begin_object();
            json.field("day", e.day);
            json.field("kind", e.kind);
            if (!e.school.empty()) {
                json.field("school", e.school);
                json.field("tide", e.tide);
            }
            json.end_object();
        }
        json.end_array();
        json.end_object();
    }

    if (world.has_terrain_detail) {
        json.key("terrain_detail");
        json.begin_object();
        json.field("version", world.terrain_detail.version);
        json.field("seed", static_cast<long long>(world.terrain_detail.seed));
        json.key("bands");
        json.begin_array();
        for (const DetailBand& b : world.terrain_detail.bands) {
            json.begin_object();
            json.field("scale_m", b.scale_m);
            json.field("amplitude_m", b.amplitude_m);
            json.field("seed", static_cast<long long>(b.seed));
            json.end_object();
        }
        json.end_array();
        json.field("height_reference",
                   std::string_view("radial metres above the reference sphere; same datum as layers.height"));
        json.field("coordinates",
                   std::string_view("unit direction (cos(lat)cos(lon), sin(lat), cos(lat)sin(lon))"));
        json.field("method", std::string_view(
            "Coarse elevation plus continuous spherical gradient-noise bands, with water, shoreline, river and "
            "flood protection. Resolved samples are authoritative; level of detail never changes the height "
            "function."));
        json.end_object();
    }

    if (world.has_ecology) {
        json.key("regions");
        json.begin_object();
        json.key("influences");
        json.begin_array();
        for (const RegionInfluence& r : world.ecology.regions) {
            json.begin_object();
            json.field("id", r.id);
            json.field("maximum", r.maximum);
            json.field("area_km2", r.area_km2);
            json.field("reason", r.reason);
            json.key("centers");
            json.begin_array();
            for (const int c : r.centers) json.value(c);
            json.end_array();
            json.end_object();
        }
        json.end_array();
        json.key("landmarks");
        json.begin_array();
        for (const Landmark& m : world.ecology.landmarks) {
            json.begin_object();
            json.field("id", m.id);
            json.field("kind", m.kind);
            json.field("x", m.x);
            json.field("z", m.z);
            json.field("layer", m.layer);
            json.field("intensity", m.intensity);
            json.end_object();
        }
        json.end_array();
        json.field("method", std::string_view(
            "Independent overlapping fields; low suitability can still be valuable to a specialist site."));
        json.end_object();

        json.key("ocean_archipelagos");
        json.begin_array();
        for (const Archipelago& a : world.ecology.archipelagos) {
            json.begin_object();
            json.field("id", a.id);
            json.field("kind", a.kind);
            json.field("eligible", a.eligible);
            json.field("island_count", static_cast<long long>(a.islands.size()));
            json.key("direction");
            json.begin_array();
            json.value(a.direction.x);
            json.value(a.direction.y);
            json.value(a.direction.z);
            json.end_array();
            json.end_object();
        }
        json.end_array();

        json.key("habitats");
        json.begin_object();
        json.field("version", 1);
        json.key("aquatic");
        json.begin_array();
        for (const char* name : {"reef", "lagoon", "estuary", "sheltered_bay", "rocky_coast", "kelp",
                                 "fjord", "open_ocean"}) {
            json.value(std::string_view(name));
        }
        json.end_array();
        json.field("method", std::string_view(
            "Depth, coast exposure, salinity and climate proxies; no tides, currents or biological reef "
            "simulation."));
        json.end_object();

        json.key("seasonal_environment");
        json.begin_object();
        json.field("month_count", static_cast<long long>(world.seasonal_months.size()));
        json.field("method", std::string_view(
            "Monthly latitude/elevation temperature proxy; snow and water ice do not modify bedrock."));
        json.end_object();
    }

    if (world.has_settlements) {
        const SettlementReport& settled = world.settlements;
        json.key("settlements");
        json.begin_object();
        json.field("version", 1);
        json.field("population_profile", world.config.population_profile);

        json.key("sites");
        json.begin_array();
        for (const SettlementSite& s : settled.sites) {
            json.begin_object();
            json.field("id", s.id);
            json.field("uid", s.uid);
            json.field("node", s.node);
            json.field("x", s.x);
            json.field("z", s.z);
            json.field("name", s.name);
            json.field("name_gloss", s.name_gloss);
            json.field("population_profile", s.population_profile);
            json.field("parent_race_id", s.parent_race_id);
            json.field("city_class", std::string_view(s.city_class == CityClass::Capital  ? "capital"
                                                      : s.city_class == CityClass::Medium ? "medium"
                                                                                          : "small"));
            json.field("classification_reason", s.classification_reason);
            json.field("founding_capital", s.founding_capital);
            json.field("founding_turn", s.founding_turn);
            json.field("founding_year", s.founding_year);
            json.field("suitability", s.suitability);
            json.field("height_m", s.height_m);
            json.field("slope_degrees", s.slope_degrees);
            json.field("temperature_c", s.temperature_c);
            json.field("moisture", s.moisture);
            json.field("flood_risk", s.flood_risk);
            json.field("resource_potential", s.resource_potential);
            // Infinity is not representable in JSON, and null is the honest reading:
            // there is no distance to water that cannot be reached.
            json.key("freshwater_distance_m");
            if (std::isfinite(s.freshwater_distance_m)) {
                json.value(s.freshwater_distance_m);
            } else {
                json.value_null();
            }
            json.field("population_estimate", s.population_estimate);
            json.field("urban_population_estimate", s.urban_population_estimate);
            json.field("rural_population_estimate", s.rural_population_estimate);
            json.key("migration_source_node");
            if (s.migration_source_node >= 0) {
                json.value(s.migration_source_node);
            } else {
                json.value_null();
            }
            json.field("source_civilization_id", s.source_civilization_id);
            json.field("migration_distance_m", s.migration_distance_m);
            json.field("cultural_branch", s.cultural_branch);
            json.field("diaspora", s.diaspora);
            json.field("diaspora_reason", s.diaspora_reason);
            json.end_object();
        }
        json.end_array();

        json.key("population_budget");
        json.begin_object();
        json.field("world_capacity", settled.budget.world_capacity);
        json.field("habitable_km2", settled.budget.ceiling.habitable_km2);
        json.field("per_city_km2", settled.budget.ceiling.per_city_km2);
        json.field("resolvable_spacing_m", settled.budget.ceiling.resolvable_spacing_m);
        json.field("spacing_city_ceiling", settled.budget.ceiling.packing);
        json.field("city_limit", settled.budget.city_limit);
        json.key("allowances");
        json.begin_object();
        for (const auto& [people, value] : settled.budget.allowances) json.field(people, value);
        json.end_object();
        json.key("weighted_habitat_km2");
        json.begin_object();
        for (const auto& [people, value] : settled.budget.weighted_habitat_km2) json.field(people, value);
        json.end_object();
        json.key("requested_cities");
        json.begin_object();
        for (const auto& [people, value] : settled.budget.requested_cities) json.field(people, value);
        json.end_object();
        json.field("method", std::string_view(
            "Capacity integrates area-weighted farming potential, climate, freshwater, slope and biome. "
            "Overlapping peoples split each cell's capacity rather than multiplying it. Residents per "
            "productive km2 is a provisional proxy, not a validated agricultural yield."));
        json.end_object();

        json.key("founding");
        json.begin_object();
        json.field("start_year", settled.founding.start_year);
        json.field("end_year", settled.founding.end_year);
        json.field("elapsed_years", settled.founding.elapsed_years);
        json.field("turns", settled.founding.turns);
        json.field("target_cities", settled.founding.target_cities);
        json.field("placed_cities", settled.founding.placed_cities);
        json.field("stop_reason", settled.founding.stop_reason);
        json.key("effective_quotas");
        json.begin_object();
        for (const auto& [people, value] : settled.founding.effective_quotas) json.field(people, value);
        json.end_object();
        json.key("diaspora_bonus_used");
        json.value(settled.founding.diaspora_bonus_used);
        json.key("used_civilizations");
        json.value(settled.founding.used_civilizations);
        // The per-round record is what answers "why is this people not on the map".
        json.key("events");
        json.begin_array();
        for (const FoundingEvent& e : settled.founding.events) {
            json.begin_object();
            json.field("turn", e.turn);
            json.field("parent_race_id", e.parent_race_id);
            json.field("status", e.status);
            json.field("radius_m", e.radius_m);
            if (e.draw_percent >= 0.0) json.field("draw_percent", e.draw_percent);
            if (e.suitability >= 0.0) json.field("suitability", e.suitability);
            if (e.node >= 0) json.field("node", e.node);
            if (!e.population_profile.empty()) json.field("population_profile", e.population_profile);
            json.end_object();
        }
        json.end_array();
        json.end_object();
        json.end_object();
    }

    if (world.has_roads) {
        json.key("roads");
        json.begin_object();
        json.field("version", 1);
        json.field("components", world.roads.components);
        json.key("routes");
        json.begin_array();
        for (const Road& road : world.roads.routes) {
            json.begin_object();
            json.field("from", road.from);
            json.field("to", road.to);
            json.field("length_m", road.length_m);
            json.field("cost", road.cost);
            json.key("nodes");
            json.begin_array();
            for (const int n : road.nodes) json.value(n);
            json.end_array();
            json.key("river_crossings");
            json.begin_array();
            for (const auto& [a, b] : road.river_crossings) {
                json.begin_array();
                json.value(a);
                json.value(b);
                json.end_array();
            }
            json.end_array();
            json.end_object();
        }
        json.end_array();
        json.field("method", std::string_view(
            "Minimum spanning forest of cheapest terrain-cost routes. Water and edges above a people's "
            "own grade limit are forbidden, and a route is refused unless both ends' peoples can walk "
            "it. River crossings are bridge candidates, not engineered bridges. Disconnected land "
            "stays disconnected."));
        json.end_object();
    }

    if (world.has_wildlife) {
        // Two blocks, never one: animals and monsters are separate passes that do not
        // read each other, and a consumer asking "what lives here" means one or the
        // other, not their union.
        const auto write_nests = [&](const char* key, const NestBlock& block) {
            json.key(key);
            json.begin_object();
            json.field("version", block.version);
            json.key("sites");
            json.begin_array();
            for (const NestSite& s : block.sites) {
                json.begin_object();
                json.field("id", s.id);
                json.field("species_id", s.species_id);
                json.field("name", s.name);
                json.field("family", s.family);
                json.field("kind", s.kind);
                json.field("size", s.size);
                json.field("role", s.role);
                json.field("tier", s.tier);
                json.field("node", s.node);
                json.field("x", s.x);
                json.field("z", s.z);
                json.field("biome", s.biome);
                json.field("den", s.den);
                json.field("real", s.real);
                // A DIAMETER lives in the catalogue; this is the placed RADIUS, already
                // halved. Publishing the catalogue value here would double every
                // territory a reader draws.
                json.field("range_m", s.range_m);
                json.field("suitability", s.suitability);
                json.end_object();
            }
            json.end_array();

            json.key("saturation");
            json.begin_array();
            for (const NestSaturation& row : block.saturation) {
                json.begin_object();
                json.field("tier", row.tier);
                json.field("cells", row.cells);
                json.field("drawn", row.drawn);
                json.field("placed", row.placed);
                json.field("refused", row.refused);
                json.end_object();
            }
            json.end_array();
            json.end_object();
        };
        write_nests("wildlife", world.wildlife);
        write_nests("beast_nests", world.beast_nests);
    }

    if (!world.systems.empty()) {
        json.key("systems");
        json.value(world.systems);
    }

    json.key("layers");
    json.begin_object();
    for (const auto& [name, grid] : world.layers) {
        json.key(name);
        json.value(grid);
    }
    json.end_object();

    json.key("warnings");
    json.value(world.warnings);

    if (include_timings) {
        json.key("timing_ms");
        json.begin_object();
        for (const auto& [name, value] : world.timing_ms) json.field(name, value);
        json.end_object();
    }

    json.end_object();
    out << '\n';
}

}  // namespace fwg
