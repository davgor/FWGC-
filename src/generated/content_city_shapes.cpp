// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/city-shapes.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/city_plan.hpp"

#include "fwg/sha256.hpp"

#include <string>

namespace fwg {
namespace {

constexpr ShapeRule kRules[] = {
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::river_access, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::bridge_feasible, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::hill, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::buildable_area_m2, 40000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.2, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 25.0, 0.0, false, true, false},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::local_slope_degrees, 0.0, 15.0, 0.0, false, true, false},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::hill, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::island, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::lagoon, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::navigable_water, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::bridge_feasible, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::local_slope_degrees, 0.0, 5.0, 0.0, false, true, false},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::local_slope_degrees, 0.0, 8.0, 0.0, false, true, false},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::oasis, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::peninsula, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::coast_access, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::local_slope_degrees, 0.0, 8.0, 0.0, false, true, false},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::ridge, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::river_access, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::river_bend, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::local_slope_degrees, 0.0, 10.0, 0.0, false, true, false},
    {ShapeFeature::buildable_area_m2, 12000.0, 0.0, 0.0, true, false, false},
    {ShapeFeature::usable_land_fraction, 0.15, 0.0, 0.0, true, false, false},
    {ShapeFeature::local_slope_degrees, 0.0, 35.0, 0.0, false, true, false},
    {ShapeFeature::coast_access, 0.0, 0.0, 1.0, false, false, true},
    {ShapeFeature::navigable_water, 0.0, 0.0, 1.0, false, false, true},
};

constexpr ShapePreference kPreferences[] = {
    {{ShapeFeature::trade_intensity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::wooded_fraction, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::defense_priority, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::regional_threat, 0.55, 0.0, 0.0, true, false, false}, 18.0},
    {{ShapeFeature::defense_priority, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::aridity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::defense_priority, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::trade_intensity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::corridor_constraint, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::trade_intensity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::planned_foundation, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::trade_intensity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::aridity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::trade_intensity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::defense_priority, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::planned_foundation, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::trade_intensity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::corridor_constraint, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::defense_priority, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::trade_intensity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::defense_priority, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::aridity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::planned_foundation, 0.5, 0.0, 0.0, true, false, false}, 8.0},
    {{ShapeFeature::trade_intensity, 0.5, 0.0, 0.0, true, false, false}, 8.0},
};

constexpr ShapeVariation kVariations[] = {
    {"aspect_ratio", 1.3, 2.8, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 2.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.15, 0.6, false},
    {"aspect_ratio", 1.1, 2.8, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 2.0, 5.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.2, 0.65, false},
    {"aspect_ratio", 1.2, 2.5, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 2.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.15, 0.55, false},
    {"aspect_ratio", 1.0, 1.5, false},
    {"block_spacing_m", 40.0, 90.0, false},
    {"district_count", 2.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.2, false},
    {"ring_count", 2.0, 3.0, true},
    {"street_curvature", 0.2, 0.55, false},
    {"aspect_ratio", 1.0, 1.5, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.35, 0.8, false},
    {"aspect_ratio", 1.0, 1.6, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.45, 0.9, false},
    {"aspect_ratio", 1.8, 3.5, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.1, 0.4, false},
    {"aspect_ratio", 1.0, 2.5, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 2.0, 5.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.3, 0.8, false},
    {"aspect_ratio", 1.0, 1.8, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.0, 0.12, false},
    {"aspect_ratio", 1.2, 2.5, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 2.0, 5.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.25, 0.7, false},
    {"aspect_ratio", 1.0, 1.6, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.25, 0.7, false},
    {"aspect_ratio", 1.2, 2.5, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.2, 0.65, false},
    {"aspect_ratio", 1.4, 2.5, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.1, 0.4, false},
    {"aspect_ratio", 2.0, 4.5, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.15, 0.5, false},
    {"aspect_ratio", 1.0, 2.0, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.3, 0.8, false},
    {"aspect_ratio", 1.2, 2.2, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.0, 0.15, false},
    {"aspect_ratio", 1.8, 3.8, false},
    {"block_spacing_m", 35.0, 90.0, false},
    {"district_count", 1.0, 3.0, true},
    {"plaza_offset_fraction", 0.0, 0.25, false},
    {"street_curvature", 0.05, 0.35, false},
};

constexpr std::string_view kSourceIds[] = {"krumlov_evaluation", "tikal", "rhodes", "carcassonne", "tunis", "mzab", "lubeck", "venice", "monpazier", "mzab", "tunis", "nessebar", "ferrara", "edinburgh", "krumlov", "chanchan", "bryggen"};

// The authored design guidance for each shape, in the shape's own key order.
// Carried because the catalogue identity hashes this prose, so it already
// decides which shape a city is given; a reader should be able to see it.
constexpr ShapeLayoutNote kLayout[] = {
    {"footprint", "Unequal lobes on opposite banks with reserved flood corridors"},
    {"street_network", "Bridge approaches link two local centers; each bank also has independent circulation"},
    {"primary_anchor", "bridgeheads"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Distributed districts around several public/ceremonial cores with retained open land"},
    {"street_network", "Connected paths and service routes link plazas; housing fills local districts rather than one solid disk"},
    {"primary_anchor", "multiple_plazas"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Smaller fortified upper lobe above a larger service and housing lobe"},
    {"street_network", "Grade-safe main ascent connects upper and lower centers; local loops remain separate"},
    {"primary_anchor", "upper_citadel"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Nested elliptical defensive belts around a civic or citadel core"},
    {"street_network", "Radial spokes and ring streets connect gates; districts fill between rings"},
    {"primary_anchor", "inner_citadel"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Prefer fill inside the active outer wall; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"fortification_mode", "concentric_active"},
    {"footprint", "Dense compact perimeter with inward-looking blocks"},
    {"street_network", "Main trading routes feed narrower connected lanes and occasional residential courts"},
    {"primary_anchor", "temple_market"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Contour-following nested outline around a high civic or sacred anchor"},
    {"street_network", "Incomplete contour loops joined by a few graded spokes"},
    {"primary_anchor", "hilltop"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Long compact island outline bounded by usable shore"},
    {"street_network", "Two longitudinal streets linked by shorter cross streets to shore access"},
    {"primary_anchor", "island_crest"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Separate buildable islands with explicitly reserved waterways"},
    {"street_network", "Water routes and footbridge links connect neighborhood centers; cargo needs quay access"},
    {"primary_anchor", "island_centers"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Rectangular core distorted at terrain boundaries"},
    {"street_network", "Intersecting main streets frame a market square with secondary lanes through blocks"},
    {"primary_anchor", "market"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Several compact inhabited lobes beside protected cultivation"},
    {"street_network", "Local loops join a shared access route; irrigation and gardens stay open"},
    {"primary_anchor", "water_source"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Compact lobed outline clipped to buildable land"},
    {"street_network", "Several approach roads converge on an offset market; smaller lanes grow between them"},
    {"primary_anchor", "market"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Compact outline clipped to the peninsula; preserve the landward neck"},
    {"street_network", "A neck-to-center main route with linked local loops"},
    {"primary_anchor", "neck_and_center"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Irregular older center attached to a more regular extension"},
    {"street_network", "Existing routes continue into a new grid or axial quarter through multiple connectors"},
    {"primary_anchor", "old_new_seam"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Elongated outline following the buildable ridge"},
    {"street_network", "One crest spine with short branching lanes; use stairs only where cart routes remain available"},
    {"primary_anchor", "ridge_end"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Lobed footprint inside safe high ground of a river bend"},
    {"street_network", "A bank-parallel route and cross-lanes connect inland civic space"},
    {"primary_anchor", "bend_interior"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Adjacent rectilinear precincts separated by through-routes"},
    {"street_network", "Public circulation remains outside precinct walls; gates connect interior courts"},
    {"primary_anchor", "precinct_courts"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
    {"footprint", "Elongated frontage with inland-growing blocks"},
    {"street_network", "Quayside street with perpendicular delivery lanes and an inland return route"},
    {"primary_anchor", "harbor"},
    {"housing_fill", "Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions."},
    {"building_fit", "Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target."},
    {"guild_hall", "Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data."},
    {"orientation", "Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors."},
};

constexpr ShapeFeatureSpec kFeatureSpecs[] = {
    {true, 0.0, 1.0},  // coast_access
    {true, 0.0, 1.0},  // navigable_water
    {true, 0.0, 1.0},  // river_access
    {true, 0.0, 1.0},  // river_bend
    {true, 0.0, 1.0},  // bridge_feasible
    {true, 0.0, 1.0},  // island
    {true, 0.0, 1.0},  // peninsula
    {true, 0.0, 1.0},  // lagoon
    {true, 0.0, 1.0},  // ridge
    {true, 0.0, 1.0},  // hill
    {true, 0.0, 1.0},  // oasis
    {false, 0.0, 1.0},  // usable_land_fraction
    {false, 0.0, 1.0},  // corridor_constraint
    {false, 0.0, 1.0},  // aridity
    {false, 0.0, 1.0},  // wooded_fraction
    {false, 0.0, 1.0},  // defense_priority
    {false, 0.0, 1.0},  // regional_threat
    {false, 0.0, 1.0},  // planned_foundation
    {false, 0.0, 1.0},  // trade_intensity
    {false, 0.0, 90.0},  // local_slope_degrees
    {false, 0.0, 1000000000000.0},  // buildable_area_m2
};

// ALREADY IN DRAW ORDER: sorted by id, because _rank ranks by id and the
// cumulative weighted walk therefore subtracts in id order, not list order.
constexpr CityShape kShapes[] = {
    {"bridge_twin", "Paired bridgehead quarters", "paired", true, 7u, 10.0, {kRules + 0, 5}, {kPreferences + 0, 1}, {kVariations + 0, 5}, {kSourceIds + 0, 1}, {kLayout + 0, 7}},
    {"ceremonial_clusters", "Ceremonial centers and residential clusters", "cluster", true, 7u, 10.0, {kRules + 5, 3}, {kPreferences + 1, 1}, {kVariations + 5, 5}, {kSourceIds + 1, 1}, {kLayout + 7, 7}},
    {"citadel_lower", "Citadel and lower town", "paired", true, 7u, 10.0, {kRules + 8, 4}, {kPreferences + 2, 1}, {kVariations + 10, 5}, {kSourceIds + 2, 1}, {kLayout + 14, 7}},
    {"concentric_enceintes", "Concentric defensive enceintes", "rings", true, 4u, 6.0, {kRules + 12, 3}, {kPreferences + 3, 2}, {kVariations + 15, 6}, {kSourceIds + 3, 1}, {kLayout + 21, 8}},
    {"courtyard_lanes", "Courtyard and shaded-lane town", "organic", true, 7u, 10.0, {kRules + 15, 4}, {kPreferences + 5, 1}, {kVariations + 21, 5}, {kSourceIds + 4, 1}, {kLayout + 29, 7}},
    {"hill_rings", "Hilltop contour rings", "contour", true, 7u, 10.0, {kRules + 19, 4}, {kPreferences + 6, 1}, {kVariations + 26, 5}, {kSourceIds + 5, 1}, {kLayout + 36, 7}},
    {"island_spines", "Island town with parallel spines", "linear", true, 7u, 10.0, {kRules + 23, 4}, {kPreferences + 7, 2}, {kVariations + 31, 5}, {kSourceIds + 6, 1}, {kLayout + 43, 7}},
    {"lagoon_network", "Connected lagoon islands", "archipelago", true, 7u, 10.0, {kRules + 27, 7}, {kPreferences + 9, 1}, {kVariations + 36, 5}, {kSourceIds + 7, 1}, {kLayout + 50, 7}},
    {"market_grid", "Planned market grid", "grid", true, 7u, 10.0, {kRules + 34, 4}, {kPreferences + 10, 2}, {kVariations + 41, 5}, {kSourceIds + 8, 1}, {kLayout + 57, 7}},
    {"oasis_cluster", "Oasis settlement clusters", "cluster", true, 7u, 10.0, {kRules + 38, 4}, {kPreferences + 12, 1}, {kVariations + 46, 5}, {kSourceIds + 9, 1}, {kLayout + 64, 7}},
    {"organic_market", "Irregular market town", "organic", true, 7u, 10.0, {kRules + 42, 3}, {kPreferences + 13, 1}, {kVariations + 51, 5}, {kSourceIds + 10, 1}, {kLayout + 71, 7}},
    {"peninsula_core", "Peninsula enclosure", "water_edge", true, 7u, 10.0, {kRules + 45, 5}, {kPreferences + 14, 1}, {kVariations + 56, 5}, {kSourceIds + 11, 1}, {kLayout + 78, 7}},
    {"planned_extension", "Older core with planned extension", "hybrid", false, 7u, 10.0, {kRules + 50, 4}, {kPreferences + 15, 2}, {kVariations + 61, 5}, {kSourceIds + 12, 1}, {kLayout + 85, 7}},
    {"ridge_fishbone", "Ridge spine and branching lanes", "linear", true, 7u, 10.0, {kRules + 54, 4}, {kPreferences + 17, 2}, {kVariations + 66, 5}, {kSourceIds + 13, 1}, {kLayout + 92, 7}},
    {"river_meander", "River-bend town", "water_edge", true, 7u, 10.0, {kRules + 58, 5}, {kPreferences + 19, 2}, {kVariations + 71, 5}, {kSourceIds + 14, 1}, {kLayout + 99, 7}},
    {"walled_compounds", "Linked enclosed compounds", "compound", true, 7u, 10.0, {kRules + 63, 4}, {kPreferences + 21, 2}, {kVariations + 76, 5}, {kSourceIds + 15, 1}, {kLayout + 106, 7}},
    {"waterfront_comb", "Waterfront spine and trade lanes", "water_edge", true, 7u, 10.0, {kRules + 67, 5}, {kPreferences + 23, 1}, {kVariations + 81, 5}, {kSourceIds + 16, 1}, {kLayout + 113, 7}},
};

// The exact bytes the identity digest is taken over. Split only because MSVC
// refuses a string literal past 65535 bytes; the concatenation is the document.
constexpr std::string_view kCanonical[] = {
    "{\"numeric_parameters_status\":\"Provisional game-design thresholds and ranges; not historical measurements.\",\"research_date\":\"2026-09-15\",\"revision\":2,\"schema\":\"fantasy-world-generator.city-shapes\",\"schema_version\":1,\"selection\":{\"algorithm\":\"sha256_weighted_v1\",\"no_match\":\"Return no shape; request better site data or another site. Never fabricate shore, bridge or dry land.\",\"notes\":\"Filter hard requirements, then weight preferences and recent local repeats. Civilizations are not locked to historical cultures. Location summaries must be supplied by a future footprint-analysis adapter.\",\"repetition_penalty\":0.75,\"required_site_fields\":[\"buildable_area_m2\",\"usable_land_fraction\",\"local_slope_degrees\"]},\"shapes\":[{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35}]},\"enabled_by_default\":true,\"family\":\"organic\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"Compact courtyard fabric, winding lanes, shaded passages and market streets.\",\"period_note\":\"North African medieval core with later additions\",\"source_ids\":[\"tunis\"]},\"id\":\"organic_market\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Compact lobed outline clipped to buildable land\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained r",
    "otation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"market\",\"street_network\":\"Several approach roads converge on an offset market; smaller lanes grow between them\"},\"name\":\"Irregular market town\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"trade_intensity\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":1.6,\"min\":1},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.7,\"min\":0.25}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"field\":\"local_slope_degrees\",\"max\":8}]},\"enabled_by_default\":true,\"family\":\"grid\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"The collection identifies a grid plan with a market square and market building.\",\"period_note\":\"Medieval bastide\",\"source_ids\":[\"monpazier\"]},\"id\":\"market_grid\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Rectangular core distorted at terrain boundaries\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access",
    ", streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"market\",\"street_network\":\"Intersecting main streets frame a market square with secondary lanes through blocks\"},\"name\":\"Planned market grid\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"planned_foundation\",\"min\":0.5}},{\"bonus\":8,\"when\":{\"field\":\"trade_intensity\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":1.8,\"min\":1},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.12,\"min\":0}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"field\":\"local_slope_degrees\",\"max\":15}]},\"enabled_by_default\":true,\"family\":\"organic\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"Compact courtyard fabric, winding lanes, shaded passages and market streets.\",\"period_note\":\"North African medieval core with later additions\",\"source_ids\":[\"tunis\"]},\"id\":\"courtyard_lanes\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink bu",
    "ildings to meet a density target.\",\"footprint\":\"Dense compact perimeter with inward-looking blocks\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"temple_market\",\"street_network\":\"Main trading routes feed narrower connected lanes and occasional residential courts\"},\"name\":\"Courtyard and shaded-lane town\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"aridity\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":1.5,\"min\":1},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.8,\"min\":0.35}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"ridge\"}]},\"enabled_by_default\":true,\"family\":\"linear\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"A ridge carries the main stree",
    "t; narrow closes branch from that spine.\",\"period_note\":\"Medieval Old Town; later buildings\",\"source_ids\":[\"edinburgh\"]},\"id\":\"ridge_fishbone\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Elongated outline following the buildable ridge\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"ridge_end\",\"street_network\":\"One crest spine with short branching lanes; use stairs only where cart routes remain available\"},\"name\":\"Ridge spine and branching lanes\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"corridor_constraint\",\"min\":0.5}},{\"bonus\":8,\"when\":{\"field\":\"defense_priority\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":4.5,\"min\":2},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.5,\"min\":0.15}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"f",
    "ield\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"hill\"}]},\"enabled_by_default\":true,\"family\":\"contour\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"Fortified settlements place houses around a central mosque; settlement, cemetery and palm-grove components form a wider system.\",\"period_note\":\"11th-century foundations and later development\",\"source_ids\":[\"mzab\"]},\"id\":\"hill_rings\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Contour-following nested outline around a high civic or sacred anchor\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"hilltop\",\"street_network\":\"Incomplete contour loops joined by a few graded spokes\"},\"name\":\"Hilltop contour rings\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"defense_priority\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":1.6,\"min\":1},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"i",
    "nteger\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.9,\"min\":0.45}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"oasis\"}]},\"enabled_by_default\":true,\"family\":\"cluster\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"Fortified settlements place houses around a central mosque; settlement, cemetery and palm-grove components form a wider system.\",\"period_note\":\"11th-century foundations and later development\",\"source_ids\":[\"mzab\"]},\"id\":\"oasis_cluster\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Several compact inhabited lobes beside protected cultivation\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"water_source\",\"street_network\":\"Local loops join a shared access route; irrigation and gardens stay open\"},\"name\":\"Oasis settlement clusters\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feas",
    "ible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"aridity\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2.5,\"min\":1.2},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":5,\"min\":2},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.7,\"min\":0.25}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"river_access\"},{\"equals\":true,\"field\":\"river_bend\"}]},\"enabled_by_default\":true,\"family\":\"water_edge\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"The town occupies a Vltava meander.\",\"period_note\":\"Medieval foundation; later additions\",\"source_ids\":[\"krumlov\"]},\"id\":\"river_meander\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Lobed footprint inside safe high ground of a river bend\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"bend_interior\",\"street_network\":\"A bank-parallel route and cross-lanes connect inland civic space\"},\"name\":\"River-bend town\",\"placement_checks\":[\"Fit footprint to the buildable",
    " mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"trade_intensity\",\"min\":0.5}},{\"bonus\":8,\"when\":{\"field\":\"defense_priority\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2,\"min\":1},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.8,\"min\":0.3}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"river_access\"},{\"equals\":true,\"field\":\"bridge_feasible\"}]},\"enabled_by_default\":true,\"family\":\"paired\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"The evaluation distinguishes the castle-side Latran and town proper across the river.\",\"period_note\":\"Medieval urban areas\",\"source_ids\":[\"krumlov_evaluation\"]},\"id\":\"bridge_twin\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Unequal lobes on opposite banks with reserved flood corridors\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",",
    "\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"bridgeheads\",\"street_network\":\"Bridge approaches link two local centers; each bank also has independent circulation\"},\"name\":\"Paired bridgehead quarters\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"trade_intensity\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2.8,\"min\":1.3},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":2},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.6,\"min\":0.15}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"island\"}]},\"enabled_by_default\":true,\"family\":\"linear\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"An elongated island outline is structured by two roughly parallel routes along its crest.\",\"period_note\":\"Medieval trading city\",\"source_ids\":[\"lubeck\"]},\"id\":\"island_spines\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Long compact island outline bounded by usable shore\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff rema",
    "in in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"island_crest\",\"street_network\":\"Two longitudinal streets linked by shorter cross streets to shore access\"},\"name\":\"Island town with parallel spines\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"trade_intensity\",\"min\":0.5}},{\"bonus\":8,\"when\":{\"field\":\"corridor_constraint\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":3.5,\"min\":1.8},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.4,\"min\":0.1}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"coast_access\"},{\"equals\":true,\"field\":\"navigable_water\"}]},\"enabled_by_default\":true,\"family\":\"water_edge\",\"history\":{\"adaptation\":\"District-scale Bryggen morphology is extrapolated to a town-wide waterfront spine; this is a design abstraction.\",\"evidence\":\"Harbour-facing trading buildings are arranged beside narrow passages.\",\"period_note\":\"Medieval trading district; repeatedly rebuilt\",\"source_ids\":[\"bryggen",
    "\"]},\"id\":\"waterfront_comb\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Elongated frontage with inland-growing blocks\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"harbor\",\"street_network\":\"Quayside street with perpendicular delivery lanes and an inland return route\"},\"name\":\"Waterfront spine and trade lanes\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"trade_intensity\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":3.8,\"min\":1.8},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.35,\"min\":0.05}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"peninsula\"},{\"equals\":true,\"field\":\"coast_access\"}]},\"enabled_by_default\":true,\"family\":\"water_edge\",\"history\":{\"adaptation\":\"The patte",
    "rn is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"A rocky Black Sea peninsula constrains the settlement footprint.\",\"period_note\":\"Ancient foundation, medieval and later layers\",\"source_ids\":[\"nessebar\"]},\"id\":\"peninsula_core\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Compact outline clipped to the peninsula; preserve the landward neck\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"neck_and_center\",\"street_network\":\"A neck-to-center main route with linked local loops\"},\"name\":\"Peninsula enclosure\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"defense_priority\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2.5,\"min\":1.2},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.65,\"min\":0.2}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":",
    "[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"lagoon\"},{\"equals\":true,\"field\":\"navigable_water\"},{\"equals\":true,\"field\":\"bridge_feasible\"},{\"field\":\"local_slope_degrees\",\"max\":5}]},\"enabled_by_default\":true,\"family\":\"archipelago\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"Islands became an interconnected urban system with canals retaining traces of the original waterways.\",\"period_note\":\"Early medieval origins and later development\",\"source_ids\":[\"venice\"]},\"id\":\"lagoon_network\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Separate buildable islands with explicitly reserved waterways\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"island_centers\",\"street_network\":\"Water routes and footbridge links connect neighborhood centers; cargo needs quay access\"},\"name\":\"Connected lagoon islands\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or re",
    "ject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"trade_intensity\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2.5,\"min\":1},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":5,\"min\":2},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.8,\"min\":0.3}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"equals\":true,\"field\":\"hill\"}]},\"enabled_by_default\":true,\"family\":\"paired\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"A fortified city separates upper institutional quarters from the lower town.\",\"period_note\":\"1309-1523 knightly occupation; later layers\",\"source_ids\":[\"rhodes\"]},\"id\":\"citadel_lower\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Smaller fortified upper lobe above a larger service and housing lobe\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"upper_citadel\",\"street_network\":\"Grade-safe main ascent connects upper and lower centers; local loops remain separate\"},\"name\":\"Citadel and lower town\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio",
    " is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"defense_priority\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2.5,\"min\":1.2},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":2},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.55,\"min\":0.15}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35}]},\"enabled_by_default\":true,\"family\":\"cluster\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"Distinct monumental ensembles combine ceremonial, administrative and residential functions in a forest setting.\",\"period_note\":\"Pre-Columbian Maya; not a European medieval town\",\"source_ids\":[\"tikal\"]},\"id\":\"ceremonial_clusters\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Distributed districts around several public/ceremonial cores with retained open land\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is",
    " allowed only on sites without directional anchors.\",\"primary_anchor\":\"multiple_plazas\",\"street_network\":\"Connected paths and service routes link plazas; housing fills local districts rather than one solid disk\"},\"name\":\"Ceremonial centers and residential clusters\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"wooded_fraction\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2.8,\"min\":1.1},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":5,\"min\":2},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.65,\"min\":0.2}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"field\":\"local_slope_degrees\",\"max\":10}]},\"enabled_by_default\":true,\"family\":\"compound\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"The urban ensemble includes large enclosed units and a hierarchy of ceremonial and other spaces.\",\"period_note\":\"Pre-Columbian Chimu\",\"source_ids\":[\"chanchan\"]},\"id\":\"walled_compounds\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Adjacent rectilinear precincts separated by through-routes\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civ",
    "ilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"precinct_courts\",\"street_network\":\"Public circulation remains outside precinct walls; gates connect interior courts\"},\"name\":\"Linked enclosed compounds\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"aridity\",\"min\":0.5}},{\"bonus\":8,\"when\":{\"field\":\"planned_foundation\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2.2,\"min\":1.2},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.15,\"min\":0}}},{\"base_weight\":6,\"city_classes\":[\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":40000},{\"field\":\"usable_land_fraction\",\"min\":0.2},{\"field\":\"local_slope_degrees\",\"max\":25}]},\"enabled_by_default\":true,\"family\":\"rings\",\"history\":{\"adaptation\":\"Game pattern for high-threat capitals: multiple intact active wall rings with gated belts between them. Not a reconstruction of Carcassonne and not a claim about any anime setting; regional threat assessments select when this form appears.\",\"evidence\":\"Concentric and successive fortification lines enclose urban fabric behind controlled gates.\",\"period_note\":\"Medieval multi-wall fortification as a reusable abstract; fa",
    "ntasy concentric belts are an explicit game adaptation\",\"source_ids\":[\"carcassonne\"]},\"id\":\"concentric_enceintes\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Nested elliptical defensive belts around a civic or citadel core\",\"fortification_mode\":\"concentric_active\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Prefer fill inside the active outer wall; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"inner_citadel\",\"street_network\":\"Radial spokes and ring streets connect gates; districts fill between rings\"},\"name\":\"Concentric defensive enceintes\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\",\"Close each defensive ring on buildable land or report an explicit incomplete perimeter.\"],\"preferences\":[{\"bonus\":18,\"when\":{\"field\":\"regional_threat\",\"min\":0.55}},{\"bonus\":8,\"when\":{\"field\":\"defense_priority\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":1.5,\"min\":1},\"block_spacing_m\":{\"max\":90,\"min\":40},\"district_count\":{\"integer\":true,\"max\":3,\"min\":2},\"plaza_offset_fraction\":{\"max\":0.2,\"min\":0},\"ring_count\":{\"integer\":true,\"max\":3,\"min\":2},\"street_curvature\":{\"max\":0.55,\"min\":0.2}}},{\"base_weight\":10,\"city_classes\":[\"small\",\"medium",
    "\",\"capital\"],\"eligibility\":{\"all\":[{\"field\":\"buildable_area_m2\",\"min\":12000},{\"field\":\"usable_land_fraction\",\"min\":0.15},{\"field\":\"local_slope_degrees\",\"max\":35},{\"field\":\"local_slope_degrees\",\"max\":8}]},\"enabled_by_default\":false,\"family\":\"hybrid\",\"history\":{\"adaptation\":\"The pattern is a reusable abstraction of the cited setting, not a reconstruction or a claim that terrain alone caused the historical form.\",\"evidence\":\"Successive planned additions developed an inherited urban core.\",\"period_note\":\"14th-16th century planning; Renaissance form\",\"source_ids\":[\"ferrara\"]},\"id\":\"planned_extension\",\"layout\":{\"building_fit\":\"Use actual building plot_m and clearance_m from buildings.json; never shrink buildings to meet a density target.\",\"footprint\":\"Irregular older center attached to a more regular extension\",\"guild_hall\":\"Reserve the civilization preset guild hall near an accessible public center; counts and staff remain in the linked building/civilization data.\",\"housing_fill\":\"Reserve required public buildings, service access, streets, drainage and public spaces first. Subdivide remaining feasible district land into housing plots; do not force fill across exclusions.\",\"orientation\":\"Fit the named ridge, bank, shore, neck or route anchors. Unconstrained rotation is allowed only on sites without directional anchors.\",\"primary_anchor\":\"old_new_seam\",\"street_network\":\"Existing routes continue into a new grid or axial quarter through multiple connectors\"},\"name\":\"Older core with planned extension\",\"placement_checks\":[\"Fit footprint to the buildable mask and available area; aspect ratio is a proposal, not permission to cross water.\",\"Verify connectivity, road grades, bridge spans and every building access width.\",\"Keep water intakes, drainage, waste and fire/service access feasible.\",\"If reservations and housing cannot fit, reduce the approved program or reject the site; do not silently drop core services.\"],\"preferences\":[{\"bonus\":8,\"when\":{\"field\":\"planned_foundation\",\"m",
    "in\":0.5}},{\"bonus\":8,\"when\":{\"field\":\"trade_intensity\",\"min\":0.5}}],\"variation\":{\"aspect_ratio\":{\"max\":2.5,\"min\":1.4},\"block_spacing_m\":{\"max\":90,\"min\":35},\"district_count\":{\"integer\":true,\"max\":3,\"min\":1},\"plaza_offset_fraction\":{\"max\":0.25,\"min\":0},\"street_curvature\":{\"max\":0.4,\"min\":0.1}}}],\"site_features\":{\"aridity\":{\"max\":1,\"min\":0,\"type\":\"number\",\"unit\":\"fraction_or_normalized_score\"},\"bridge_feasible\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"buildable_area_m2\":{\"description\":\"Usable dry land after water, flood, cliff and protected-land exclusions, not a bounding rectangle.\",\"max\":1000000000000.0,\"min\":0,\"type\":\"number\",\"unit\":\"square_metres\"},\"coast_access\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"corridor_constraint\":{\"max\":1,\"min\":0,\"type\":\"number\",\"unit\":\"fraction_or_normalized_score\"},\"defense_priority\":{\"max\":1,\"min\":0,\"type\":\"number\",\"unit\":\"fraction_or_normalized_score\"},\"hill\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"island\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"lagoon\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"local_slope_degrees\":{\"description\":\"Representative slope across the proposed footprint; per-road grades still need local checks.\",\"max\":90,\"min\":0,\"type\":\"number\",\"unit\":\"degrees\"},\"navigable_water\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"oasis\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"peninsula\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"planned_foundation\":{\"max\":1,\"min\":0,\"type\":\"number\",\"unit\":\"fraction_or_normalized_score\"},\"regional_threat\":{\"description\":\"Standing regional threat from fantasy nests and dominant leyline potency after beasties/age assessments.\",\"max\":1,\"min\":0,\"type\":\"number\",\"unit\":\"fraction_or_normalized_score\"},\"ridge\":{\"missin",
    "g\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"river_access\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"river_bend\":{\"missing\":\"fails a hard requirement; no preference bonus\",\"type\":\"boolean\"},\"trade_intensity\":{\"max\":1,\"min\":0,\"type\":\"number\",\"unit\":\"fraction_or_normalized_score\"},\"usable_land_fraction\":{\"max\":1,\"min\":0,\"type\":\"number\",\"unit\":\"fraction_or_normalized_score\"},\"wooded_fraction\":{\"max\":1,\"min\":0,\"type\":\"number\",\"unit\":\"fraction_or_normalized_score\"}},\"sources\":{\"bryggen\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"Harbour-facing trading buildings are arranged beside narrow passages.\",\"title\":\"Bryggen - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/59\"},\"carcassonne\":{\"accessed\":\"2026-09-17\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"Concentric and successive fortification lines enclose urban fabric behind controlled gates.\",\"title\":\"Historic Fortified City of Carcassonne - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/345\"},\"chanchan\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"The urban ensemble includes large enclosed units and a hierarchy of ceremonial and other spaces.\",\"title\":\"Chan Chan Archaeological Zone - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/366\"},\"edinburgh\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"A ridge carries the main street; narrow closes branch from that spine.\",\"title\":\"Old and New Towns of Edinburgh - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/728\"},\"ferrara\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"Successive planned additions developed an inherited urban core.\",\"title\":\"Ferrara, City of the Renaissance - UNESCO\",\"url\":\"https://whc.u",
    "nesco.org/en/list/733\"},\"krumlov\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"The town occupies a Vltava meander.\",\"title\":\"Historic Centre of Cesky Krumlov - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/617\"},\"krumlov_evaluation\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"The evaluation distinguishes the castle-side Latran and town proper across the river.\",\"title\":\"Cesky Krumlov advisory evaluation - ICOMOS / UNESCO\",\"url\":\"https://whc.unesco.org/document/153890\"},\"lubeck\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"An elongated island outline is structured by two roughly parallel routes along its crest.\",\"title\":\"Hanseatic City of Lubeck - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/272\"},\"monpazier\":{\"accessed\":\"2026-09-15\",\"publisher\":\"Cornell University Library\",\"supports\":\"The collection identifies a grid plan with a market square and market building.\",\"title\":\"Monpazier - Cornell University, John Reps Collection\",\"url\":\"https://digital.library.cornell.edu/catalog/ss%3A3852783\"},\"mzab\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"Fortified settlements place houses around a central mosque; settlement, cemetery and palm-grove components form a wider system.\",\"title\":\"M'Zab Valley - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/188\"},\"nessebar\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"A rocky Black Sea peninsula constrains the settlement footprint.\",\"title\":\"Ancient City of Nessebar - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/217\"},\"rhodes\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"A fortified city separates upper institutional quar",
    "ters from the lower town.\",\"title\":\"Medieval City of Rhodes - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/493\"},\"tikal\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"Distinct monumental ensembles combine ceremonial, administrative and residential functions in a forest setting.\",\"title\":\"Tikal National Park - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/64\"},\"tunis\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"Compact courtyard fabric, winding lanes, shaded passages and market streets.\",\"title\":\"Medina of Tunis - UNESCO Urban Heritage Atlas\",\"url\":\"https://whc.unesco.org/en/activities/1176/\"},\"venice\":{\"accessed\":\"2026-09-15\",\"publisher\":\"UNESCO World Heritage Centre / nominated heritage documentation\",\"supports\":\"Islands became an interconnected urban system with canals retaining traces of the original waterways.\",\"title\":\"Venice and its Lagoon - UNESCO\",\"url\":\"https://whc.unesco.org/en/list/394\"}},\"status\":\"planner_input_not_runtime_geometry\",\"units\":\"metres unless a field states otherwise\"}",
};

}  // namespace

std::span<const CityShape> city_shapes() { return kShapes; }

std::span<const ShapeFeatureSpec> shape_feature_specs() { return kFeatureSpecs; }

int city_shape_catalogue_revision() { return 2; }

double shape_repetition_penalty() { return 0.75; }

// Hashed here rather than recorded, so the digest cannot outlive the bytes it
// describes. data/city-shapes.json records the expected value and the codegen
// refuses a mismatch; this reproduces it from the embedded document.
const std::string& city_shape_catalogue_identity() {
    static const std::string identity = [] {
        std::string document;
        for (std::string_view chunk : kCanonical) document.append(chunk);
        return sha256_hex(document);
    }();
    return identity;
}

}  // namespace fwg
