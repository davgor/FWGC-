#include "fwg/serialize.hpp"

#include <algorithm>
#include <cmath>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/json.hpp"
#include "serialize_blocks.hpp"
#include "serialize_document.hpp"

namespace fwg {
namespace {

void write_vec3(JsonWriter& json, const Vec3& v) {
    json.begin_array();
    json.value(v.x);
    json.value(v.y);
    json.value(v.z);
    json.end_array();
}

// One ley line as the reference publishes it: its ends, the great-circle `path` between the
// two key points they index, its id and intensity. In the magic block's flat `edges` list the
// ends are offset past earlier networks' nodes and the line names its network and repeats its
// intensity as `strength`; the caller passes `network` for that list only.
void write_ley_edge(JsonWriter& json, const LeyNetwork& net, const LeyEdge& e, int offset,
                    const std::string* network) {
    json.begin_object();
    json.field("from", e.from + offset);
    json.field("to", e.to + offset);
    json.key("path");
    json.begin_array();
    const auto count = static_cast<int>(net.nodes.size());
    if (e.from >= 0 && e.from < count && e.to >= 0 && e.to < count) {
        for (const Vec3& p : arc_path(net.nodes[static_cast<std::size_t>(e.from)].direction,
                                      net.nodes[static_cast<std::size_t>(e.to)].direction)) {
            write_vec3(json, p);
        }
    }
    json.end_array();
    json.field("id", e.id);
    json.field("intensity", e.intensity);
    if (network != nullptr) {
        json.field("network", *network);
        json.field("strength", e.intensity);
    }
    json.end_object();
}

// The reference's `config` and `effective_config` are its Config dataclass, so both are the
// dataclass's fields in its order (world options live in `world_options`, as the reference
// keeps them). Two keys are this port's own, kept deliberately: whether the world_size preset
// built the world and the width that did (docs/inherited-findings.md #17), because an explicit
// circumference_km or globe_radius bypasses the preset the dataclass still names.
void write_config(JsonWriter& json, const Config& cfg, bool effective) {
    json.begin_object();
    serialize_detail::write_config_fields(json, cfg, effective);
    json.field("circumference_m", cfg.circumference_m);
    json.field("world_size_was_used", cfg.world_size_was_used);
    json.end_object();
}

}  // namespace

namespace serialize_detail {

void write_natural_catalogue(JsonWriter& json) {
    json.begin_array();
    for (const NaturalBiome& b : natural_biomes()) {
        json.begin_object();
        json.field("id", b.id);
        json.field("core", b.core);
        json.field("name", b.name);
        json.key("color");
        json.begin_array();
        for (const int c : b.color) json.value(c);
        json.end_array();
        json.field("asset_id", b.asset_id);
        json.end_object();
    }
    json.end_array();
}

void write_magical_catalogue(JsonWriter& json) {
    const auto schools = magic_school_table();
    json.begin_array();
    for (const BiomeVariant& v : biome_variant_catalogue()) {
        const int school = magic_school_index(v.magic_school);
        json.begin_object();
        json.field("id", v.id);
        json.field("core", v.core);
        json.field("core_biome_id", v.core_biome_id);
        json.field("magic_school", v.magic_school);
        json.field("group", v.group);
        json.field("descriptors",
                   school >= 0 ? schools[static_cast<std::size_t>(school)].descriptors : std::string_view());
        json.field("name", v.name);
        json.key("color");
        json.begin_array();
        for (const int c : v.color) json.value(c);
        json.end_array();
        json.field("asset_id", v.asset_id);
        json.end_object();
    }
    json.end_array();
}

// The datum every height in the detail definition and in a height tile is measured from:
// one sentence, so the definition and the tiles sampled with it cannot disagree about it.
constexpr std::string_view kHeightReference =
    "radial metres above the reference sphere; same datum as layers.height";

void write_terrain_detail(JsonWriter& json, const TerrainDetail& detail) {
    json.begin_object();
    json.field("version", detail.version);
    json.field("seed", static_cast<long long>(detail.seed));
    // No per-band seed: the reference derives each at sample time as
    // child_seed(seed, str(index)), and so does this port's sampler (detail.cpp), so the
    // definition is the root seed and the bands' scale and amplitude, as published there.
    json.key("bands");
    json.begin_array();
    for (const DetailBand& b : detail.bands) {
        json.begin_object();
        json.field("scale_m", b.scale_m);
        json.field("amplitude_m", b.amplitude_m);
        json.end_object();
    }
    json.end_array();
    json.field("height_reference", kHeightReference);
    json.field("coordinates",
               std::string_view("unit direction (cos(lat)cos(lon), sin(lat), cos(lat)sin(lon))"));
    json.field("method", std::string_view(
        "Coarse elevation plus continuous spherical gradient-noise bands, with water, shoreline, river and "
        "flood protection. Resolved samples are authoritative; level of detail never changes the height "
        "function."));
    json.end_object();
}

void write_founding_event(JsonWriter& json, const FoundingEvent& e) {
    json.begin_object();
    json.field("turn", e.turn);
    json.field("parent_race_id", e.parent_race_id);
    json.field("status", e.status);
    json.field("radius_m", e.radius_m);
    if (e.required_separation_degrees >= 0.0) json.field("required_separation_degrees", e.required_separation_degrees);
    if (e.draw_percent >= 0.0) json.field("draw_percent", e.draw_percent);
    if (e.suitability >= 0.0) json.field("suitability", e.suitability);
    if (e.node >= 0) json.field("node", e.node);
    if (!e.population_profile.empty()) json.field("population_profile", e.population_profile);
    // A founding carries the site it placed, as the reference's event does: its event is the
    // site's record plus the round's fields. The diaspora three only on a diaspora founding.
    if (e.node >= 0) {
        json.field("founding_turn", e.founding_turn);
        json.field("founding_capital", e.founding_capital);
        json.field("founding_year", e.founding_year);
        json.key("migration_source_node");
        if (e.migration_source_node >= 0) {
            json.value(e.migration_source_node);
        } else {
            json.value_null();
        }
        json.key("source_civilization_id");
        if (e.source_civilization_id.empty()) {
            json.value_null();
        } else {
            json.value(std::string_view(e.source_civilization_id));
        }
        json.field("migration_distance_m", e.migration_distance_m);
        json.field("cultural_branch", e.cultural_branch);
        if (e.diaspora) {
            json.field("diaspora", e.diaspora);
            json.field("diaspora_reason", e.diaspora_reason);
            json.field("diaspora_bonus", e.diaspora_bonus);
        }
    }
    json.end_object();
}

// The rest of a site record as the reference writes it. The three diaspora keys are null on a
// city no diaspora founded, as its founding record's .get() reads them; a player's city carries
// no suitability terms until a rebuild founds around it. `layer` and `mobility` arrive with the
// society pass (write_site_society_fields).
void write_site_reference_fields(JsonWriter& json, const SettlementSite& s) {
    const auto null_unless = [&](const char* key, bool present, const auto& value) {
        json.key(key);
        if (present) {
            json.value(value);
        } else {
            json.value_null();
        }
    };
    null_unless("diaspora", s.diaspora, true);
    null_unless("diaspora_reason", s.diaspora, std::string_view(s.diaspora_reason));
    null_unless("diaspora_bonus", s.diaspora, s.diaspora_bonus);
    json.field("civilization_id", s.population_profile);
    json.key("direction");
    write_vec3(json, s.direction);
    json.field("kind", std::string_view("city"));
    // The reference's outpost list is never filled: no site is an outpost.
    json.field("outpost", false);
    json.field("reason", s.reason);
    if (!s.suitability_factors.empty()) {
        json.key("suitability_factors");
        json.begin_object();
        for (const SuitabilityFactor& f : s.suitability_factors) json.field(f.name, f.value);
        json.end_object();
    }
    json.field("founded_age", s.founded_age);
    json.field("source_culture", s.source_culture);
}

void write_settlements(JsonWriter& json, const World& world) {
    const SettlementReport& settled = world.settlements;
    json.begin_object();
    json.field("version", kSettlementsVersion);
    json.field("population_profile", world.config.population_profile);

    json.key("sites");
    json.begin_array();
    for (std::size_t site_index = 0; site_index < settled.sites.size(); ++site_index) {
        const SettlementSite& s = settled.sites[site_index];
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
        write_site_reference_fields(json, s);
        write_site_society_fields(json, world, site_index);
        write_site_age_fields(json, world, site_index);
        write_site_layout_fields(json, world, site_index);
        json.end_object();
    }
    json.end_array();

    // The budget is its own top-level block, `population_budget`, as the reference writes
    // it. The copy that used to sit here under this port's own names had no reader.

    json.key("founding");
    json.begin_object();
    json.field("version", kFoundingVersion);
    json.key("minimum_residents");
    json.begin_object();
    for (const auto& [people, value] : settled.founding.minimum_residents) json.field(people, value);
    json.end_object();
    json.field("diaspora_interval_years", settled.founding.diaspora_interval_years);
    json.field("years_per_round", settled.founding.years_per_round);
    json.field("origin_min_separation_degrees", settled.founding.origin_min_separation_degrees);
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
    for (const FoundingEvent& e : settled.founding.events) write_founding_event(json, e);
    json.end_array();
    json.field("definition", kFoundingDefinition);
    json.end_object();

    // The master seed the stage's streams are named children of -- founding, naming -- and
    // the cities the peoples asked for in all (the budget's requested cities, summed).
    json.field("seed", static_cast<long long>(world.config.seed));
    long long requested = 0;
    for (const auto& [people, cities] : settled.budget.requested_cities) {
        (void)people;
        requested += cities;
    }
    json.field("requested", requested);
    std::string method(kSettlementsMethod);
    if (world.has_magic) method += kSettlementsMagicMethod;
    json.field("method", method);
    json.end_object();
}

}  // namespace serialize_detail

void write_world_json(std::ostream& out, const World& world, bool pretty, bool include_timings) {
    DocumentOptions options;
    options.pretty = pretty;
    options.include_timings = include_timings;
    write_world_json(out, world, options);
}

void write_world_json(std::ostream& out, const World& world, const DocumentOptions& options) {
    JsonWriter json(out, options.pretty);
    json.begin_object();

    json.field("schema", std::string_view("fwg.world"));
    json.field("schema_version", kWorldSchemaVersion);
    json.field("generator_version", world.generator_version);
    json.field("completed_stage", world.completed_stage);

    json.key("config");
    write_config(json, world.config, /*effective=*/false);
    json.key("effective_config");
    write_config(json, world.effective, /*effective=*/true);

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
    // The plates and their motion units are published where the reference publishes them,
    // in `phases`; the top-level copies this document used to carry had no reader.

    // The ladder, the request, and the peoples the request named. None of the four is a
    // stage's output: `population` and `population_profiles` exist from stage 1 in the
    // reference, and `phases` and `recipe` describe the whole run.
    json.key("phases");
    serialize_detail::write_phases(json, world);
    json.key("recipe");
    serialize_detail::write_recipe(json, world);
    json.key("population");
    serialize_detail::write_population(json, world);
    json.key("population_profiles");
    serialize_detail::write_population_profiles(json);

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
        // The unique spherical nodes as [x, z], in node order: what `receivers` and the river
        // segments index. The grid's seam column and pole rows are not nodes of their own.
        {
            const auto grid = sphere_grid(world.config.size, world.effective.globe_radius);
            json.key("nodes");
            json.begin_array();
            for (std::size_t i = 0; i < grid->node_count(); ++i) {
                json.begin_array();
                json.value(grid->point_x[i]);
                json.value(grid->point_z[i]);
                json.end_array();
            }
            json.end_array();
        }
        json.field("ocean_km2", world.water.ocean_km2);
        json.field("lake_km2", world.water.lake_km2);
        json.field("dry_km2", world.water.dry_km2);
        json.field("lake_capacity_m3", world.water.lake_capacity_m3);
        json.field("has_ocean", world.water.has_ocean);
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
        // `biomes` and `natural_biomes` are the same catalogue under two names, as the
        // reference publishes them.
        json.key("biomes");
        serialize_detail::write_natural_catalogue(json);
        json.key("natural_biomes");
        serialize_detail::write_natural_catalogue(json);
        json.key("landforms");
        json.begin_array();
        for (int i = 0; i < 4; ++i) json.value(std::string_view(landform_name(i)));
        json.end_array();
        json.field("climate_seed", static_cast<long long>(world.terrain.climate_seed));
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
        // What the biome-variant pass adds, from the moment it has run: the catalogue
        // `biome_variant` indexes into, and the contract between the two biome layers.
        if (world.layers.count("biome_variant") != 0) {
            json.key("magical_biomes");
            serialize_detail::write_magical_catalogue(json);
            json.field("biome_contract", std::string_view(
                "biome and natural_biome are natural catalogue IDs, never array offsets; biome_variant is a "
                "magical catalogue index or -1. Food and habitat rules use the natural core and explicit variant "
                "ID with independent school risk."));
        }
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
        // The school groups, each once, in school order.
        json.key("groups");
        json.begin_array();
        {
            std::vector<std::string_view> groups;
            for (const MagicSchool& s : magic_school_table()) {
                if (std::find(groups.begin(), groups.end(), s.group) == groups.end()) groups.push_back(s.group);
            }
            for (const std::string_view g : groups) json.value(g);
        }
        json.end_array();
        // A network publishes no `manifested` or `hidden` flag, as the reference's does not:
        // `distribution` is null exactly when generation did not raise the network, and
        // `school_order` lists the four hidden schools after the known eight.
        json.key("networks");
        json.begin_object();
        for (const MagicSchool& school : magic_school_table()) {
            const LeyNetwork& net = world.magic.networks.at(std::string(school.name));
            json.key(net.name);
            json.begin_object();
            json.field("name", net.name);
            json.field("group", net.group);
            json.field("descriptors", net.descriptors);
            json.key("color");
            json.begin_array();
            for (const int c : net.color) json.value(c);
            json.end_array();
            json.field("seed", static_cast<long long>(net.seed));
            json.field("strength", net.strength);
            json.field("width_m", net.width_m);
            json.field("instability", net.instability);
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
            for (const LeyEdge& e : net.edges) write_ley_edge(json, net, e, 0, nullptr);
            json.end_array();
            json.key("distribution");
            if (net.manifested) {
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
            } else {
                json.value_null();
            }
            if (net.has_surge) json.field("surged_strength", net.surged_strength);
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
        // The colleges live in the magic block, as they do in the reference.
        serialize_detail::write_college_fields(json, world);
        // Every network's key points and lines in one list, in school order: a line's ends
        // index `nodes`, offset past the networks before it, and it names its network and
        // carries its intensity again as `strength`, as the reference's flat view does.
        json.key("nodes");
        json.begin_array();
        for (const MagicSchool& school : magic_school_table()) {
            for (const LeyNode& node : world.magic.networks.at(std::string(school.name)).nodes) {
                write_vec3(json, node.direction);
            }
        }
        json.end_array();
        json.key("edges");
        json.begin_array();
        {
            int offset = 0;
            for (const MagicSchool& school : magic_school_table()) {
                const LeyNetwork& net = world.magic.networks.at(std::string(school.name));
                for (const LeyEdge& e : net.edges) write_ley_edge(json, net, e, offset, &net.name);
                offset += static_cast<int>(net.nodes.size());
            }
        }
        json.end_array();
        // The sky's surge on the networks' strengths: the day the time API last read, or on a
        // world it has never touched, the reported year's first day that the astrology pass
        // read. Known schools only; a hidden school takes no surge.
        if (world.lunar_surge) {
            json.key("lunar_surge");
            json.begin_object();
            json.field("day", world.lunar_surge->day);
            json.key("factors");
            json.begin_object();
            for (const auto& [school, factor] : world.lunar_surge->factors) json.field(school, factor);
            json.end_object();
            json.field("formula", world.lunar_surge->formula);
            json.end_object();
        } else if (world.has_astrology) {
            const auto schools = magic_school_table();
            json.key("lunar_surge");
            json.begin_object();
            json.field("day", static_cast<long long>(world.lunar_almanac.now.day));
            json.key("factors");
            json.begin_object();
            for (int s = 0; s < kKnownSchools; ++s) {
                json.field(schools[static_cast<std::size_t>(s)].name,
                           world.lunar_almanac.now.tide[static_cast<std::size_t>(s)]);
            }
            json.end_object();
            json.field("formula", kLunarSurgeFormula);
            json.end_object();
        }
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
        // Stage 7's addition, once the relics have been carved.
        if (std::find(world.systems.begin(), world.systems.end(), "relic_carving") != world.systems.end()) {
            json.field("relic_method", std::string_view(
                "Compare old and rerouted river occupancy and dry former water footprints. Incise beds/shoulders, "
                "then reroute again; refilled features retain relic provenance."));
        }
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
        // A day's sky, into an object the caller has opened: `now` leads with its day, and a
        // month heads with its first_day instead, as the reference splats the same state.
        const auto day_fields = [&](JsonWriter& w, const DayState& d) {
            w.field("phase_fraction", d.phase_fraction);
            w.field("elongation_degrees", d.elongation_degrees);
            w.field("illumination", d.illumination);
            w.field("moonrise_hour", d.moonrise_hour);
            w.field("spin_fraction", d.spin_fraction);
            w.field("nod_degrees", d.nod_degrees);
            w.field("leaning", d.leaning);
            w.key("tide");
            tide_object(w, d.tide);
        };
        const auto range = [](JsonWriter& w, std::string_view key, auto low, auto high) {
            w.key(key);
            w.begin_array();
            w.value(low);
            w.value(high);
            w.end_array();
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
        // What the request chose for the moon: its variation, and only the cycles it pinned.
        json.key("controls");
        json.begin_object();
        json.field("variation", static_cast<long long>(world.moon.variation));
        json.key("overrides");
        json.begin_object();
        const MoonOverrides& pinned = world.moon.overrides;
        if (pinned.synodic_days != 0) json.field("synodic", pinned.synodic_days);
        if (pinned.spin_days != 0) json.field("spin", pinned.spin_days);
        if (pinned.nod_days != 0) json.field("nod", pinned.nod_days);
        if (pinned.tilt_degrees != 0.0) json.field("tilt", pinned.tilt_degrees);
        json.end_object();
        json.end_object();
        json.key("hemispheres");
        json.begin_object();
        json.key("still");
        json.begin_array();
        for (const std::string_view s : kStillHemisphere) json.value(s);
        json.end_array();
        json.key("restless");
        json.begin_array();
        for (const std::string_view s : kRestlessHemisphere) json.value(s);
        json.end_array();
        json.end_object();
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
        json.key("period_ranges");
        json.begin_object();
        range(json, "synodic", kSynodicDaysRange[0], kSynodicDaysRange[1]);
        range(json, "spin", kSpinDaysRange[0], kSpinDaysRange[1]);
        range(json, "nod", kNodDaysRange[0], kNodDaysRange[1]);
        json.end_object();
        range(json, "tilt_range_degrees", kTiltDegreesRange[0], kTiltDegreesRange[1]);
        json.end_object();
        json.key("formulas");
        json.begin_object();
        for (const AstrologyFormula& f : kAstrologyFormulas) json.field(f.key, f.text);
        json.end_object();
        json.field("method", std::string_view(
            "One moon, not tidally locked. Its still hemisphere carries radiant, water, earth and umbral; its "
            "restless hemisphere weave, fire, air and infernal, one school per quarter. Seeded integer-day "
            "phase, spin and nod cycles prefer coprime periods, so the great year is their least common "
            "multiple. A school surges when its quarter both faces the world and is lit."));
        json.field("limits", kAstrologyLimits);
        json.end_object();

        json.key("lunar_almanac");
        json.begin_object();
        json.field("version", 1);
        json.field("reported_year", world.lunar_almanac.reported_year);
        json.field("reported_year_source", world.lunar_almanac.reported_year_source);
        // None when no day inside the great year restarts all three cycles together.
        json.key("next_grand_alignment_day");
        if (world.lunar_almanac.next_grand_alignment_day >= 0) {
            json.value(static_cast<long long>(world.lunar_almanac.next_grand_alignment_day));
        } else {
            json.value_null();
        }
        json.key("now");
        json.begin_object();
        json.field("day", static_cast<long long>(world.lunar_almanac.now.day));
        day_fields(json, world.lunar_almanac.now);
        json.end_object();
        json.key("months");
        json.begin_array();
        for (const AlmanacMonth& m : world.lunar_almanac.months) {
            json.begin_object();
            json.field("first_day", static_cast<long long>(m.first_day));
            day_fields(json, m.state);
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
        serialize_detail::write_terrain_detail(json, world.terrain_detail);
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
            if (m.conventional_suitability) {
                json.field("conventional_suitability", *m.conventional_suitability);
                json.key("reason");
                json.value(std::string_view(m.reason));
            }
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
            json.key("direction");
            write_vec3(json, a.direction);
            json.key("islands");
            json.begin_array();
            for (const Island& island : a.islands) {
                json.begin_object();
                json.key("direction");
                write_vec3(json, island.direction);
                json.field("angular_radius", island.angular_radius);
                json.end_object();
            }
            json.end_array();
            json.field("eligible", a.eligible);
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
        json.key("months");
        json.begin_array();
        for (const SeasonalMonth& month : world.seasonal_months) {
            json.begin_object();
            json.key("temperature");
            json.value(month.temperature);
            json.key("snow");
            json.value(month.snow);
            json.key("water_ice");
            json.value(month.water_ice);
            json.end_object();
        }
        json.end_array();
        json.field("method", std::string_view(
            "Monthly latitude/elevation temperature proxy; snow and water ice do not modify bedrock."));
        json.end_object();
    }

    if (world.has_settlements) {
        json.key("settlements");
        serialize_detail::write_settlements(json, world);
        // Three blocks the reference writes beside the settlements rather than inside
        // them, all from stage 10 and rewritten by every rebuild.
        json.key("population_budget");
        serialize_detail::write_population_budget(json, world);
        json.key("peoples");
        serialize_detail::write_peoples(json, world);
        json.key("civilizations");
        serialize_detail::write_civilizations(json, world);
    }

    if (world.has_roads) {
        json.key("roads");
        json.begin_object();
        json.field("version", 1);
        json.field("components", world.roads.components);
        json.key("routes");
        json.begin_array();
        for (std::size_t route_index = 0; route_index < world.roads.routes.size(); ++route_index) {
            const Road& road = world.roads.routes[route_index];
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
            // What the caravans that ride this road left on it (stage 16).
            serialize_detail::write_route_trade_fields(json, world, route_index);
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
        const auto optional_metres = [&](const char* key, const std::optional<double>& v) {
            json.key(key);
            if (v && std::isfinite(*v)) {
                json.value(*v);
            } else {
                json.value_null();
            }
        };
        const auto write_nests = [&](const char* key, const NestBlock& block, CreatureClass which) {
            const bool animals = which == CreatureClass::Animal;
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
                json.field("node", s.node);
                json.field("x", s.x);
                json.field("z", s.z);
                json.field("layer", kNestSiteLayer);
                json.key("direction");
                write_vec3(json, s.direction);
                json.field("biome", s.biome);
                json.field("tier", s.tier);
                json.field("family", s.family);
                json.field("kind", s.kind);
                json.field("size", s.size);
                // A monster has no feeding role: null, as the reference's p.get('role').
                json.key("role");
                if (s.role.empty()) {
                    json.value_null();
                } else {
                    json.value(s.role);
                }
                json.field("suitability", s.suitability);
                // A DIAMETER lives in the catalogue; this is the placed RADIUS, already
                // halved. Publishing the catalogue value here would double every
                // territory a reader draws.
                json.field("range_m", s.range_m);
                json.field("class", std::string_view(s.creature_class == CreatureClass::Animal ? "animal"
                                                                                                : "monster"));
                json.field("den", s.den);
                json.field("real", s.real);
                json.end_object();
            }
            json.end_array();

            // Why each species is or is not in this world. The animal rows name the feeding
            // role; a monster has none and its row has no such key.
            json.key("diagnostics");
            json.begin_array();
            for (const NestDiagnostic& d : block.diagnostics) {
                json.begin_object();
                json.field("species_id", d.species_id);
                json.field("name", d.name);
                json.field("tier", d.tier);
                if (animals) {
                    json.key("role");
                    if (d.role.empty()) {
                        json.value_null();
                    } else {
                        json.value(d.role);
                    }
                }
                json.field("placed", d.placed);
                json.field("cells", d.cells);
                json.field("reason", to_string(d.reason));
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
                // Null where nothing is settled or the tier drew or kept nothing.
                optional_metres("rate_distance_m", row.rate_distance_m);
                optional_metres("placed_distance_m", row.placed_distance_m);
                json.end_object();
            }
            json.end_array();

            json.key("reachability");
            json.begin_object();
            json.field("authored", block.reachability.authored);
            json.field("eligible", block.reachability.eligible);
            json.field("placed", block.reachability.placed);
            json.field("never_satisfiable", block.reachability.never_satisfiable);
            json.field("scope", nest_reachability_scope());
            json.end_object();

            // The pass's catalogue rows exactly as authored, and wildlife's feeding roles.
            json.key("profiles");
            serialize_detail::write_nest_profiles(json, which);
            json.field("catalogue_count", block.catalogue_count);
            if (animals) {
                json.key("roles");
                serialize_detail::write_nest_roles(json);
            }
            json.field("method", block.method);
            json.field("limits", block.limits);
            // Stamped by an age transition: the age whose ground the nests stand on.
            if (block.evaluated_age) json.field("evaluated_age", *block.evaluated_age);
            json.end_object();
        };
        write_nests("wildlife", world.wildlife, CreatureClass::Animal);
        write_nests("beast_nests", world.beast_nests, CreatureClass::Monster);
    }

    // Stages 12, 13 and 16: the hinterland and its economy, the faiths, the plans, the
    // bands and herds, the cast, the rosters and the key locations.
    serialize_detail::write_late_blocks(json, world);

    // Stage 16: every plan and the regional roads in one globe-centred frame.
    if (world.has_world_scene) {
        json.key("world_scene");
        serialize_detail::write_world_scene(json, world.world_scene);
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

    if (options.include_timings) {
        json.key("timing_ms");
        json.begin_object();
        for (const auto& [name, value] : world.timing_ms) json.field(name, value);
        json.end_object();
    }

    // Opt-in, as the reference's --include-build-stages is: several times the world's own
    // size, and read by the stage inspector alone.
    if (options.build_stages != nullptr) {
        json.key("build_stages");
        serialize_detail::write_build_stages(json, *options.build_stages);
    }

    // Last, as the reference computes it: a summary of the finished world.
    json.key("debug_stats");
    serialize_detail::write_debug_stats(json, world);

    json.end_object();
    out << '\n';
}

void write_height_tile_json(std::ostream& out, const World& world, const HeightTile& tile, bool pretty) {
    JsonWriter json(out, pretty);
    json.begin_object();
    json.field("schema", std::string_view("fantasy-world-generator.height-tile"));
    json.field("version", kHeightTileVersion);
    json.field("source_sha256", std::string_view(tile.source_sha256));
    json.field("generator_version", world.generator_version);
    json.key("terrain_detail");
    serialize_detail::write_terrain_detail(json, world.terrain_detail);
    json.field("level", tile.level);
    json.key("origin_index");
    json.begin_array();
    json.value(tile.origin_x);
    json.value(tile.origin_z);
    json.end_array();
    json.field("size", tile.size);
    json.key("global_cells");
    json.begin_array();
    json.value(tile.global_cols);
    json.value(tile.global_rows);
    json.end_array();
    json.field("radius_m", tile.radius_m);
    json.field("angular_step_degrees", tile.angular_step_degrees);
    json.field("equatorial_spacing_m", tile.equatorial_spacing_m);
    json.field("coordinates",
               std::string_view("row-major; latitude=90-180*(origin_z+row)/2^level; "
                                "longitude=-180+180*(origin_x+column)/2^level; "
                                "globe Cartesian=(radius_m+height_m)*unit_direction"));
    json.field("height_reference", serialize_detail::kHeightReference);
    json.key("heights_m");
    json.begin_array();
    for (const std::vector<double>& row : tile.heights_m) {
        json.begin_array();
        for (const double h : row) json.value(h);
        json.end_array();
    }
    json.end_array();
    json.end_object();
    out << '\n';
}

}  // namespace fwg
