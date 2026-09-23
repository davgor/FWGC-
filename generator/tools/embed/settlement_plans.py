"""data/settlement-plans.json -> src/generated/content_settlement_plans.cpp.

THE PLAN LISTS ARE ORDER-SENSITIVE AND NO RNG IS INVOLVED. The city and hamlet
planners walk `buildings` in list order, split it into core-priority and the rest, and
greedily install each row into the FIRST free candidate plot, stamping a positional id
(plot-0, plot-1, ...). Reordering a plan list therefore moves buildings onto different
plots, renumbers every plot id, and changes WHICH rows fail to place when the site runs
out of terrain-safe ground. Nothing here is sorted. Append, never insert.

`entities` list order is the registry's civilization_order, which is what the profile
list returns and what a map colours regions by. Also preserved verbatim.

Each row is emitted as the MERGE the reference performs at load, `{**structure, **row}`:
the row supplies structure_id, name, count and placement_conditions, and the structure
supplies priority, plot_m, dimensions_m and staffing. The merge is a structure INDEX
rather than a copy, so the geometry has exactly one home -- see PresetRow::structure().

Infrastructure rows carry no `name` and no `count` at all (they have `quantity_mode`
instead), which is why a count of zero here means "the row authored none", not "build
none of it": the fortifier reads a wall's dimensions and never its count.
"""
import json

from embed.common import BANNER, DATA, OUT, cpp_string

# The closed set the loader validates against. An unknown condition is refused at load
# in the reference; refused here instead, where the data is what would be wrong.
PLACEMENT_CONDITIONS = ("usable_groundwater", "water_collection_or_delivery", "navigable_shore",
                        "flowing_water", "reliable_wind", "ore_and_fuel_supply", "food_surplus",
                        "working_animals", "clay_and_fuel_supply", "medicinal_supply",
                        "clear_sky_view", "isolation_access", "defended_perimeter",
                        "route_crossing", "grade_change", "retention_needed",
                        "farming_support", "resource_support")

BLOCKS = ("small_city", "medium_city", "capital_city", "hamlet")


def load_settlement_plans():
    doc = json.loads((DATA / "settlement-plans.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("settlement-plans.json must declare schema version 1")

    buildings = json.loads((DATA / "buildings.json").read_text(encoding="utf-8"))["registry"]
    libraries = {}
    for library, block in buildings["structure_blocks"].items():
        for group in block["blocks"]:
            for structure in group["structures"]:
                libraries[structure["id"]] = library

    entities = doc["entities"]
    if not entities:
        raise SystemExit("settlement-plans.json has no civilization")
    ids = [e["id"] for e in entities]
    if len(set(ids)) != len(ids):
        raise SystemExit("settlement-plans.json has a duplicate civilization id")

    fallback = buildings["building_packs"]["fallback_pack_id"]
    for entity in entities:
        bound = entity["buildings"]
        if fallback not in bound["packs"]:
            # Every people must be able to build SOMETHING on ground no themed pack wants.
            raise SystemExit("%s does not bind the fallback pack %s" % (entity["id"], fallback))
        if "rural" not in bound["structure_block_ids"]:
            raise SystemExit("%s does not bind the rural library; its hamlets could not be built"
                             % entity["id"])
        available = set(bound["structure_block_ids"])
        for block in BLOCKS:
            if block not in entity:
                raise SystemExit("%s has no %s plan" % (entity["id"], block))
            for group in ("buildings", "infrastructure"):
                for row in entity[block].get(group, []):
                    sid = row["structure_id"]
                    if sid not in libraries:
                        raise SystemExit("%s/%s names unknown structure %s" % (entity["id"], block, sid))
                    if libraries[sid] not in available:
                        raise SystemExit("%s/%s names %s from the %s library, which this people "
                                         "does not bind" % (entity["id"], block, sid, libraries[sid]))
                    unknown = [c for c in row["placement_conditions"] if c not in PLACEMENT_CONDITIONS]
                    if unknown:
                        raise SystemExit("%s/%s/%s names unknown placement condition(s) %s"
                                         % (entity["id"], block, sid, unknown))
                    if group == "buildings" and row.get("count", 0) <= 0:
                        raise SystemExit("%s/%s/%s asks for %r of a building"
                                         % (entity["id"], block, sid, row.get("count")))
    return doc, buildings


def render_settlement_plans():
    doc, buildings = load_settlement_plans()

    # The structure index a plan row joins on. The generated structures table lays the
    # three libraries end to end in catalogue order, so this reproduces that walk.
    index_of = {}
    for library in ("common", "rural", "castle"):
        for group in buildings["structure_blocks"][library]["blocks"]:
            for structure in group["structures"]:
                index_of[structure["id"]] = len(index_of)

    conditions, rows = [], []
    blocks, entities = [], []
    for entity in doc["entities"]:
        block_begin = len(blocks)
        for block in BLOCKS:
            spans = []
            for group in ("buildings", "infrastructure"):
                begin = len(rows)
                for row in entity[block].get(group, []):
                    condition_begin = len(conditions)
                    for condition in row["placement_conditions"]:
                        conditions.append("PlacementCondition::%s" % condition)
                    name = row.get("name")
                    rows.append("    {%du, %s, %d, {kConditions + %d, %d}},"
                                % (index_of[row["structure_id"]],
                                   cpp_string(name) if name is not None else "{}",
                                   int(row.get("count", 0)),
                                   condition_begin, len(conditions) - condition_begin))
                spans.append((begin, len(rows) - begin))
            blocks.append("    {{kPresetRows + %d, %d}, {kPresetRows + %d, %d}},"
                          % (spans[0][0], spans[0][1], spans[1][0], spans[1][1]))
        entities.append("    {%s, %s, {kPresets + %d, %d}},"
                        % (cpp_string(entity["id"]), cpp_string(entity["parent_race_id"]),
                           block_begin, len(blocks) - block_begin))

    lines = [BANNER.format(source="settlement-plans.json")]
    lines.append('#include "fwg/city_plan.hpp"\n')
    lines.append("namespace fwg {")
    lines.append("namespace {\n")
    lines.append("constexpr PlacementCondition kConditions[] = {%s};\n"
                 % (", ".join(conditions) if conditions
                    else "PlacementCondition::usable_groundwater"))
    lines.append("// ORDER IS THE CONTRACT AND THERE IS NO RNG. Rows are installed greedily into")
    lines.append("// the first free plot in this order, and the plot ids are positional, so an")
    lines.append("// insertion moves buildings and renumbers every plot after it.")
    lines.append("constexpr PresetRow kPresetRows[] = {")
    lines.extend(rows)
    lines.append("};\n")
    lines.append("// Four blocks per civilization, in SettlementBlock order.")
    lines.append("constexpr SettlementPreset kPresets[] = {")
    lines.extend(blocks)
    lines.append("};\n")
    lines.append("// The registry's civilization_order. Append, never insert.")
    lines.append("constexpr CivilizationPresets kCivilizations[] = {")
    lines.extend(entities)
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::span<const CivilizationPresets> civilization_presets() { return kCivilizations; }\n")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {
    OUT / "content_settlement_plans.cpp": render_settlement_plans,
}
