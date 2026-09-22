#include "fwg/serialize.hpp"

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
    json.field("mountain_abundance", cfg.mountain_abundance);
    json.field("erosion_passes", cfg.erosion_passes);
    json.field("erosion_strength", cfg.erosion_strength);
    json.field("wind_bearing", cfg.wind_bearing);
    json.field("rain_passes", cfg.rain_passes);
    json.field("rain_strength", cfg.rain_strength);
    json.field("temperature_offset", cfg.temperature_offset);
    json.field("moisture_bias", cfg.moisture_bias);
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
