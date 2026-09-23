// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/castles.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/castle_plan.hpp"

#include "fwg/sha256.hpp"

#include <optional>
#include <string>

namespace fwg {

// data/castles.json records the identity its digest was taken under. A planner at
// another version publishes an identity that file never recorded.
static_assert(kCastlePlanVersion == 3, "data/castles.json records planner_version 3");
static_assert(kCastleGeometryVersion == 1, "data/castles.json records geometry_version 1");

namespace {

constexpr std::string_view kEnds[] = {
    "curtain_end",
    "curtain_end",
    "corner_join",
    "gate_join",
    "tower_join",
    "stair_join",
    "gate_join",
    "curtain_end",
    "curtain_end",
    "curtain_end",
    "curtain_end",
};

// Sorted by key: every lookup is by name, so the order is free.
constexpr CastleModule kModules[] = {
    {"bailey_court", "module.bailey_court", "building.bailey_court", "court", "", std::nullopt, CastleBand{0.0, 0.0, 0.0}, false, {kEnds + 0, 0}},
    {"corner_join", "module.corner_join", "building.corner_tower", "node", "corner_join", CastleBand{2.5, 4.0, 3.0}, CastleBand{10.0, 18.0, 14.0}, true, {kEnds + 0, 1}},
    {"curtain_segment", "module.curtain_segment", "building.curtain_segment", "curtain", "curtain_end", CastleBand{2.5, 4.0, 3.0}, CastleBand{6.0, 10.0, 8.0}, true, {kEnds + 1, 5}},
    {"drawbridge", "module.drawbridge", "building.drawbridge", "node", "gate_join", CastleBand{2.5, 4.0, 3.0}, CastleBand{1.0, 4.0, 2.0}, false, {kEnds + 6, 1}},
    {"gatehouse", "module.gatehouse", "building.barbican", "node", "gate_join", CastleBand{2.5, 4.0, 3.0}, CastleBand{10.0, 16.0, 14.0}, true, {kEnds + 7, 1}},
    {"interval_tower", "module.interval_tower", "building.corner_tower", "node", "tower_join", CastleBand{2.5, 4.0, 3.0}, CastleBand{10.0, 18.0, 14.0}, true, {kEnds + 8, 1}},
    {"keep_tower", "module.keep_tower", "building.keep_tower", "landmark", "", std::nullopt, CastleBand{18.0, 28.0, 24.0}, false, {kEnds + 9, 0}},
    {"postern", "module.postern", "building.postern", "node", "gate_join", CastleBand{2.5, 4.0, 3.0}, CastleBand{6.0, 10.0, 8.0}, true, {kEnds + 9, 1}},
    {"wall_stair", "module.wall_stair", "building.wall_stair", "node", "stair_join", CastleBand{2.5, 4.0, 3.0}, CastleBand{6.0, 10.0, 8.0}, true, {kEnds + 10, 1}},
};

constexpr CastleJoinRules kJoinRules = {0.5, 1.0, 25.0, 135.0, 10.0, true};

constexpr CastleRingSpec kRings[] = {
    {"outer", 1.0, false, "gatehouse", true, 2, 4},
    {"bailey", 1.0, true, "gatehouse", true, 2, 4},
    {"motte", 0.38, false, "postern", false, 1, 2},
    {"outer", 1.0, true, "gatehouse", true, 2, 6},
    {"inner", 0.55, false, "gatehouse", false, 2, 4},
};

constexpr CastleCourtSpec kCourts[] = {
    {"bailey", "outer", "building.bailey_court"},
    {"bailey", "bailey", "building.bailey_court"},
    {"motte_yard", "motte", "building.bailey_court"},
    {"outer_bailey", "outer", "building.bailey_court"},
    {"inner_ward", "inner", "building.bailey_court"},
};

constexpr CastleLandmarkSpec kLandmarks[] = {
    {"building.keep_tower", "outer", 0.0},
    {"building.keep_tower", "motte", 0.0},
    {"building.keep_tower", "inner", 0.0},
};

constexpr CastleServiceSpec kServices[] = {
    {"building.castle_barracks", 1},
    {"building.castle_armory", 1},
    {"building.castle_well", 1},
    {"building.great_hall", 1},
    {"building.castle_barracks", 1},
    {"building.castle_armory", 1},
    {"building.castle_well", 1},
    {"building.castle_chapel", 1},
    {"building.great_hall", 1},
    {"building.drawbridge", 1},
    {"building.castle_barracks", 1},
    {"building.castle_armory", 1},
    {"building.castle_well", 1},
    {"building.castle_chapel", 1},
    {"building.great_hall", 1},
    {"building.barbican", 1},
    {"building.drawbridge", 1},
};

// FILE ORDER. One uniform draw is walked cumulatively along this list, so a
// reordered or extended list hands different fortresses different kits.
constexpr CastleKit kKits[] = {
    {"kit.simple_enclosure", "Simple enclosure", 4, 72.0, {kRings + 0, 1}, {kCourts + 0, 1}, {kLandmarks + 0, 1}, {kServices + 0, 4}},
    {"kit.motte_bailey", "Motte and bailey", 3, 88.0, {kRings + 1, 2}, {kCourts + 1, 2}, {kLandmarks + 1, 1}, {kServices + 4, 6}},
    {"kit.concentric", "Concentric wards", 2, 96.0, {kRings + 3, 2}, {kCourts + 3, 2}, {kLandmarks + 2, 1}, {kServices + 10, 7}},
};

constexpr std::string_view kRegistryCanonical[] = {
    "{\"description\":\"Independent fortification modules, join rules and typology kits for castle_plans. Does not participate in city or hamlet planner identity.\",\"join_rules\":{\"max_height_delta_m\":1.0,\"max_thickness_delta_m\":0.5,\"max_turn_degrees\":135,\"min_turn_degrees_for_corner\":25,\"segment_depth_m\":10,\"walkway_required\":true},\"kits\":[{\"courts\":[{\"ring_role\":\"outer\",\"role\":\"bailey\",\"structure_id\":\"building.bailey_court\"}],\"half_m\":72,\"id\":\"kit.simple_enclosure\",\"landmarks\":[{\"offset_scale\":0.0,\"ring_role\":\"outer\",\"structure_id\":\"building.keep_tower\"}],\"name\":\"Simple enclosure\",\"rings\":[{\"ditch\":false,\"gate\":\"gatehouse\",\"postern\":true,\"role\":\"outer\",\"scale\":1.0,\"stairs\":2,\"tower_budget\":4}],\"services\":[{\"count\":1,\"structure_id\":\"building.castle_barracks\"},{\"count\":1,\"structure_id\":\"building.castle_armory\"},{\"count\":1,\"structure_id\":\"building.castle_well\"},{\"count\":1,\"structure_id\":\"building.great_hall\"}],\"weight\":4},{\"courts\":[{\"ring_role\":\"bailey\",\"role\":\"bailey\",\"structure_id\":\"building.bailey_court\"},{\"ring_role\":\"motte\",\"role\":\"motte_yard\",\"structure_id\":\"building.bailey_court\"}],\"half_m\":88,\"id\":\"kit.motte_bailey\",\"landmarks\":[{\"offset_scale\":0.0,\"ring_role\":\"motte\",\"structure_id\":\"building.keep_tower\"}],\"name\":\"Motte and bailey\",\"rings\":[{\"ditch\":true,\"gate\":\"gatehouse\",\"postern\":true,\"role\":\"bailey\",\"scale\":1.0,\"stairs\":2,\"tower_budget\":4},{\"ditch\":false,\"gate\":\"postern\",\"postern\":false,\"role\":\"motte\",\"scale\":0.38,\"stairs\":1,\"tower_budget\":2}],\"services\":[{\"count\":1,\"structure_id\":\"building.castle_barracks\"},{\"count\":1,\"structure_id\":\"building.castle_armory\"},{\"count\":1,\"structure_id\":\"building.castle_well\"},{\"count\":1,\"structure_id\":\"building.castle_chapel\"},{\"count\":1,\"structure_id\":\"building.great_hall\"},{\"count\":1,\"structure_id\":\"building.drawbridge\"}],\"weight\":3},{\"courts\":[{\"ring_role\":\"outer\",\"role\":\"outer_bailey\",\"structure_id\":\"building.bailey_court\"},{\"ring_role\":\"inner\",\"role\":\"inner_ward\",\"structure_id\":\"building.bailey_court\"}],\"half_m\":96,\"id\":\"kit.c",
    "oncentric\",\"landmarks\":[{\"offset_scale\":0.0,\"ring_role\":\"inner\",\"structure_id\":\"building.keep_tower\"}],\"name\":\"Concentric wards\",\"rings\":[{\"ditch\":true,\"gate\":\"gatehouse\",\"postern\":true,\"role\":\"outer\",\"scale\":1.0,\"stairs\":2,\"tower_budget\":6},{\"ditch\":false,\"gate\":\"gatehouse\",\"postern\":false,\"role\":\"inner\",\"scale\":0.55,\"stairs\":2,\"tower_budget\":4}],\"services\":[{\"count\":1,\"structure_id\":\"building.castle_barracks\"},{\"count\":1,\"structure_id\":\"building.castle_armory\"},{\"count\":1,\"structure_id\":\"building.castle_well\"},{\"count\":1,\"structure_id\":\"building.castle_chapel\"},{\"count\":1,\"structure_id\":\"building.great_hall\"},{\"count\":1,\"structure_id\":\"building.barbican\"},{\"count\":1,\"structure_id\":\"building.drawbridge\"}],\"weight\":2}],\"modules\":{\"bailey_court\":{\"compatible_ends\":[],\"end_type\":null,\"height_m\":{\"max\":0.0,\"min\":0.0,\"nominal\":0.0},\"id\":\"module.bailey_court\",\"role\":\"court\",\"structure_id\":\"building.bailey_court\",\"thickness_m\":null,\"walkway\":false},\"corner_join\":{\"compatible_ends\":[\"curtain_end\"],\"end_type\":\"corner_join\",\"height_m\":{\"max\":18.0,\"min\":10.0,\"nominal\":14.0},\"id\":\"module.corner_join\",\"role\":\"node\",\"structure_id\":\"building.corner_tower\",\"thickness_m\":{\"max\":4.0,\"min\":2.5,\"nominal\":3.0},\"walkway\":true},\"curtain_segment\":{\"compatible_ends\":[\"curtain_end\",\"corner_join\",\"gate_join\",\"tower_join\",\"stair_join\"],\"end_type\":\"curtain_end\",\"height_m\":{\"max\":10.0,\"min\":6.0,\"nominal\":8.0},\"id\":\"module.curtain_segment\",\"role\":\"curtain\",\"structure_id\":\"building.curtain_segment\",\"thickness_m\":{\"max\":4.0,\"min\":2.5,\"nominal\":3.0},\"walkway\":true},\"drawbridge\":{\"compatible_ends\":[\"gate_join\"],\"end_type\":\"gate_join\",\"height_m\":{\"max\":4.0,\"min\":1.0,\"nominal\":2.0},\"id\":\"module.drawbridge\",\"role\":\"node\",\"structure_id\":\"building.drawbridge\",\"thickness_m\":{\"max\":4.0,\"min\":2.5,\"nominal\":3.0},\"walkway\":false},\"gatehouse\":{\"compatible_ends\":[\"curtain_end\"],\"end_type\":\"gate_join\",\"height_m\":{\"max\":16.0,\"min\":10.0,\"nominal\":14.0},\"id\":\"module.gatehouse\",\"role\":\"node\",\"structure_id\":\"building",
    ".barbican\",\"thickness_m\":{\"max\":4.0,\"min\":2.5,\"nominal\":3.0},\"walkway\":true},\"interval_tower\":{\"compatible_ends\":[\"curtain_end\"],\"end_type\":\"tower_join\",\"height_m\":{\"max\":18.0,\"min\":10.0,\"nominal\":14.0},\"id\":\"module.interval_tower\",\"role\":\"node\",\"structure_id\":\"building.corner_tower\",\"thickness_m\":{\"max\":4.0,\"min\":2.5,\"nominal\":3.0},\"walkway\":true},\"keep_tower\":{\"compatible_ends\":[],\"end_type\":null,\"height_m\":{\"max\":28.0,\"min\":18.0,\"nominal\":24.0},\"id\":\"module.keep_tower\",\"role\":\"landmark\",\"structure_id\":\"building.keep_tower\",\"thickness_m\":null,\"walkway\":false},\"postern\":{\"compatible_ends\":[\"curtain_end\"],\"end_type\":\"gate_join\",\"height_m\":{\"max\":10.0,\"min\":6.0,\"nominal\":8.0},\"id\":\"module.postern\",\"role\":\"node\",\"structure_id\":\"building.postern\",\"thickness_m\":{\"max\":4.0,\"min\":2.5,\"nominal\":3.0},\"walkway\":true},\"wall_stair\":{\"compatible_ends\":[\"curtain_end\"],\"end_type\":\"stair_join\",\"height_m\":{\"max\":10.0,\"min\":6.0,\"nominal\":8.0},\"id\":\"module.wall_stair\",\"role\":\"node\",\"structure_id\":\"building.wall_stair\",\"thickness_m\":{\"max\":4.0,\"min\":2.5,\"nominal\":3.0},\"walkway\":true}},\"revision\":1,\"schema\":\"fantasy-world-generator.castle-registry\",\"schema_version\":1,\"unit\":\"metres\"}",
};

constexpr std::string_view kStructuresCanonical[] = {
    "{\"building.bailey_court\":{\"access\":{\"minimum_clear_width_m\":4,\"mode\":\"cart\"},\"clearance_m\":{\"front\":0,\"left\":0,\"rear\":0,\"right\":0},\"dimensions_m\":{\"depth\":44,\"height\":0,\"width\":36},\"geometry_type\":\"open_area\",\"id\":\"building.bailey_court\",\"module_family\":\"open_space\",\"name\":\"Bailey courtyard\",\"placement\":\"Interior of a closed fortification ring.\",\"plot_m\":{\"depth\":44,\"width\":36},\"priority\":\"core\",\"purpose\":\"Open ward for circulation, musters and service yards inside a ring.\",\"requirements\":[\"Closed wall network.\"],\"reuse_candidates\":[],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Open court; no dedicated roster.\",\"roles\":[]},\"status\":\"planned\"},\"building.barbican\":{\"access\":{\"minimum_clear_width_m\":3.5,\"mode\":\"cart\"},\"clearance_m\":{\"front\":8,\"left\":2,\"rear\":4,\"right\":2},\"dimensions_m\":{\"depth\":16,\"height\":14,\"width\":14},\"geometry_type\":\"building\",\"id\":\"building.barbican\",\"module_family\":\"fortification\",\"name\":\"Barbican gatehouse\",\"placement\":\"Outer ring at parent-city approach.\",\"plot_m\":{\"depth\":28,\"width\":18},\"priority\":\"core\",\"purpose\":\"Primary defended entrance on the approach road.\",\"requirements\":[\"Gate clearance; curtain joins on both flanks.\"],\"reuse_candidates\":[\"production.arch-011\",\"building.gatehouse\"],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Gate roster; sleeping deferred.\",\"roles\":[{\"maximum\":2,\"minimum\":1,\"role\":\"gate_captain\",\"target\":1},{\"maximum\":6,\"minimum\":2,\"role\":\"gate_guard\",\"target\":4}]},\"status\":\"planned\"},\"building.castle_armory\":{\"access\":{\"minimum_clear_width_m\":1.5,\"mode\":\"service\"},\"clearance_m\":{\"front\":4,\"left\":2,\"rear\":2,\"right\":2},\"dimensions_m\":{\"depth\":12,\"height\":7,\"width\":8},\"geometry_type\":\"building\",\"id\":\"building.castle_armory\",\"module_family\":\"storehouse\",\"name\":\"Castle armory\",\"placement\":\"Guarded bailey near barracks.\",\"plot_m\":{\"depth\":18,\"width\":12},\"priority\":\"core\",\"purpose\":\"Secure arms store for the garrison.\",\"requirements\":[],\"reuse_ca",
    "ndidates\":[\"building.armory\"],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Armorer post only.\",\"roles\":[{\"maximum\":2,\"minimum\":1,\"role\":\"armorer\",\"target\":1}]},\"status\":\"planned\"},\"building.castle_barracks\":{\"access\":{\"minimum_clear_width_m\":2,\"mode\":\"foot\"},\"clearance_m\":{\"front\":4,\"left\":2,\"rear\":3,\"right\":2},\"dimensions_m\":{\"depth\":18,\"height\":7,\"width\":12},\"geometry_type\":\"building\",\"id\":\"building.castle_barracks\",\"module_family\":\"military_hall\",\"name\":\"Castle barracks\",\"placement\":\"Outer or main bailey near the gate.\",\"plot_m\":{\"depth\":25,\"width\":16},\"priority\":\"core\",\"purpose\":\"Garrison muster and equipment space inside the bailey.\",\"requirements\":[],\"reuse_candidates\":[\"building.barracks\"],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Muster roster; sleeping deferred.\",\"roles\":[{\"maximum\":3,\"minimum\":1,\"role\":\"sergeant\",\"target\":2},{\"maximum\":12,\"minimum\":4,\"role\":\"garrison_trooper\",\"target\":8}]},\"status\":\"planned\"},\"building.castle_chapel\":{\"access\":{\"minimum_clear_width_m\":1.5,\"mode\":\"foot\"},\"clearance_m\":{\"front\":3,\"left\":2,\"rear\":2,\"right\":2},\"dimensions_m\":{\"depth\":12,\"height\":9,\"width\":8},\"geometry_type\":\"building\",\"id\":\"building.castle_chapel\",\"module_family\":\"landmark\",\"name\":\"Castle chapel\",\"placement\":\"Inner bailey.\",\"plot_m\":{\"depth\":17,\"width\":12},\"priority\":\"conditional\",\"purpose\":\"Small chapel for garrison and household rites.\",\"requirements\":[],\"reuse_candidates\":[],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Single chaplain post.\",\"roles\":[{\"maximum\":1,\"minimum\":1,\"role\":\"chaplain\",\"target\":1}]},\"status\":\"planned\"},\"building.castle_well\":{\"access\":{\"minimum_clear_width_m\":1.2,\"mode\":\"foot\"},\"clearance_m\":{\"front\":2,\"left\":2,\"rear\":2,\"right\":2},\"dimensions_m\":{\"depth\":3,\"height\":3,\"width\":3},\"geometry_type\":\"building\",\"id\":\"building.castle_well\",\"module_family\":\"service_yard\",\"name\":\"Castle well\",\"placement\":\"Inner bailey or mott",
    "e yard.\",\"plot_m\":{\"depth\":7,\"width\":7},\"priority\":\"core\",\"purpose\":\"Independent water supply inside the defended precinct.\",\"requirements\":[],\"reuse_candidates\":[\"building.well\"],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"No dedicated roster.\",\"roles\":[]},\"status\":\"planned\"},\"building.corner_tower\":{\"access\":{\"minimum_clear_width_m\":1.5,\"mode\":\"service\"},\"clearance_m\":{\"front\":2,\"left\":2,\"rear\":2,\"right\":2},\"dimensions_m\":{\"depth\":8,\"height\":14,\"width\":8},\"geometry_type\":\"building\",\"id\":\"building.corner_tower\",\"module_family\":\"fortification\",\"name\":\"Castle corner tower\",\"placement\":\"Perimeter turns exceeding the kit corner angle.\",\"plot_m\":{\"depth\":12,\"width\":12},\"priority\":\"core\",\"purpose\":\"Join curtain runs at turns and provide elevated defence.\",\"requirements\":[\"Curtain ends compatible with tower join thickness and height.\"],\"reuse_candidates\":[\"production.arch-013\",\"building.tower\"],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Watch posts only; sleeping deferred.\",\"roles\":[{\"maximum\":3,\"minimum\":1,\"role\":\"tower_watch\",\"target\":2}]},\"status\":\"planned\"},\"building.curtain_segment\":{\"access\":{\"minimum_clear_width_m\":1.5,\"mode\":\"service\"},\"clearance_m\":{\"front\":0,\"left\":2,\"rear\":0,\"right\":2},\"dimensions_m\":{\"depth\":10,\"height\":8,\"width\":3},\"geometry_type\":\"linear_segment\",\"id\":\"building.curtain_segment\",\"module_family\":\"linear_fortification\",\"name\":\"Curtain wall segment\",\"placement\":\"Along closed bailey or ward perimeter.\",\"plot_m\":{\"depth\":10,\"width\":7},\"priority\":\"core\",\"purpose\":\"Walkable curtain run with parapet between fortification nodes.\",\"requirements\":[\"Compatible joins at both ends; continuous walkway.\"],\"reuse_candidates\":[\"production.arch-012\",\"building.wall\"],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"No dedicated roster. Patrols are counted at barracks or gate nodes.\",\"roles\":[]},\"status\":\"planned\"},\"building.drawbridge\":{\"access\":{\"",
    "minimum_clear_width_m\":3.5,\"mode\":\"cart\"},\"clearance_m\":{\"front\":2,\"left\":2,\"rear\":2,\"right\":2},\"dimensions_m\":{\"depth\":14,\"height\":2,\"width\":6},\"geometry_type\":\"building\",\"id\":\"building.drawbridge\",\"module_family\":\"fortification\",\"name\":\"Drawbridge and ditch crossing\",\"placement\":\"Immediately outside the barbican when the kit requests a ditch.\",\"plot_m\":{\"depth\":18,\"width\":10},\"priority\":\"conditional\",\"purpose\":\"Span the outer ditch on the primary approach.\",\"requirements\":[\"Outer ditch present; gate clearance.\"],\"reuse_candidates\":[],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Operated by gate crew.\",\"roles\":[]},\"status\":\"planned\"},\"building.great_hall\":{\"access\":{\"minimum_clear_width_m\":2,\"mode\":\"foot\"},\"clearance_m\":{\"front\":4,\"left\":2,\"rear\":3,\"right\":2},\"dimensions_m\":{\"depth\":24,\"height\":10,\"width\":12},\"geometry_type\":\"building\",\"id\":\"building.great_hall\",\"module_family\":\"hall\",\"name\":\"Castle great hall\",\"placement\":\"Inner bailey near the keep.\",\"plot_m\":{\"depth\":31,\"width\":16},\"priority\":\"specialist\",\"purpose\":\"Assembly and household hall inside the bailey.\",\"requirements\":[\"Bailey court.\"],\"reuse_candidates\":[],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Household service roster.\",\"roles\":[{\"maximum\":2,\"minimum\":1,\"role\":\"steward\",\"target\":1},{\"maximum\":4,\"minimum\":1,\"role\":\"hall_attendant\",\"target\":2}]},\"status\":\"planned\"},\"building.keep_tower\":{\"access\":{\"minimum_clear_width_m\":2,\"mode\":\"foot\"},\"clearance_m\":{\"front\":4,\"left\":4,\"rear\":4,\"right\":4},\"dimensions_m\":{\"depth\":16,\"height\":24,\"width\":16},\"geometry_type\":\"building\",\"id\":\"building.keep_tower\",\"module_family\":\"landmark\",\"name\":\"Keep tower\",\"placement\":\"Inner ward or motte centre.\",\"plot_m\":{\"depth\":24,\"width\":24},\"priority\":\"specialist\",\"purpose\":\"Command and refuge tower without an embedded courtyard.\",\"requirements\":[\"Bailey or motte court available.\"],\"reuse_candidates\":[\"production.arch-014\",\"building.keep\"],\"staffi",
    "ng\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Command roster; residential rooms deferred.\",\"roles\":[{\"maximum\":3,\"minimum\":1,\"role\":\"commander\",\"target\":2},{\"maximum\":9,\"minimum\":3,\"role\":\"keep_guard\",\"target\":6}]},\"status\":\"planned\"},\"building.postern\":{\"access\":{\"minimum_clear_width_m\":1.5,\"mode\":\"foot\"},\"clearance_m\":{\"front\":4,\"left\":2,\"rear\":2,\"right\":2},\"dimensions_m\":{\"depth\":8,\"height\":8,\"width\":6},\"geometry_type\":\"building\",\"id\":\"building.postern\",\"module_family\":\"fortification\",\"name\":\"Postern gate\",\"placement\":\"Outer ring opposite primary approach.\",\"plot_m\":{\"depth\":14,\"width\":10},\"priority\":\"conditional\",\"purpose\":\"Secondary personnel exit opposite the main gate.\",\"requirements\":[\"Closed perimeter; restricted clearance.\"],\"reuse_candidates\":[],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"Guard posts only.\",\"roles\":[{\"maximum\":2,\"minimum\":1,\"role\":\"postern_guard\",\"target\":2}]},\"status\":\"planned\"},\"building.wall_stair\":{\"access\":{\"minimum_clear_width_m\":1.2,\"mode\":\"foot\"},\"clearance_m\":{\"front\":2,\"left\":1,\"rear\":1,\"right\":1},\"dimensions_m\":{\"depth\":6,\"height\":8,\"width\":4},\"geometry_type\":\"building\",\"id\":\"building.wall_stair\",\"module_family\":\"fortification\",\"name\":\"Wall-walk stair\",\"placement\":\"Interior face of a curtain run.\",\"plot_m\":{\"depth\":9,\"width\":6},\"priority\":\"core\",\"purpose\":\"Connect bailey grade to the curtain walkway.\",\"requirements\":[\"Walkway continuity through the adjoining segments.\"],\"reuse_candidates\":[],\"staffing\":{\"basis\":\"distinct_people_all_shifts\",\"housing_location\":\"city\",\"notes\":\"No dedicated roster.\",\"roles\":[]},\"status\":\"planned\"}}",
};

std::string joined(std::span<const std::string_view> chunks) {
    std::string document;
    for (std::string_view chunk : chunks) document.append(chunk);
    return document;
}

}  // namespace

std::span<const CastleModule> castle_modules() { return kModules; }

const CastleJoinRules& castle_join_rules() { return kJoinRules; }

std::span<const CastleKit> castle_kits() { return kKits; }

int castle_registry_revision() { return 1; }

// Hashed here rather than recorded, so the digest cannot outlive the bytes it
// describes. The codegen has already refused bytes whose digest differs from the
// one data/castles.json records.
const std::string& castle_registry_identity() {
    static const std::string identity = sha256_hex(joined(kRegistryCanonical));
    return identity;
}

const std::string& castle_structures_identity() {
    static const std::string identity = sha256_hex(joined(kStructuresCanonical));
    return identity;
}

}  // namespace fwg
