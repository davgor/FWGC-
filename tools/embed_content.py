#!/usr/bin/env python3
"""Turn the content JSON under data/ into typed C++ tables under src/generated/.

    python tools/embed_content.py [--check]

Why generate rather than parse at runtime:

  * The library has no dependencies and no JSON *reader*, and adding one so that a
    thirteen-row table can be read at startup is a poor trade. Generated tables are
    typed, zero-cost and need no file to exist next to the binary -- which is what
    lets this drop into an engine build unchanged.
  * Content stays content. `data/*.json` is the source of truth, diffable and
    versioned, and nobody hand-edits a table in a .cpp.
  * The generated files are CHECKED IN, so building needs no Python.

`--check` regenerates into memory and fails if the checked-in output is stale. Run it
before committing a data change.

Validation here is deliberately fail-closed and mirrors the Python loader's: a registry
that has lost a row or a name is not a smaller catalogue, it is a broken one.
"""
import argparse
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
OUT = ROOT / "src" / "generated"

BANNER = """// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/{source}.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.
"""


def cpp_string(text):
    out = text.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + out.replace("\n", "\\n").replace("\t", "\\t") + '"'


def load_biomes():
    doc = json.loads((DATA / "biomes.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("biomes.json must declare schema version 1")

    rows = doc["natural_biomes"]
    if len({r["id"] for r in rows}) != len(rows):
        raise SystemExit("biomes.json has a duplicate natural biome id")

    allowed = {"id", "core", "color", "reachable", "unreachable_note"}
    for r in rows:
        extra = set(r) - allowed
        if extra:
            raise SystemExit("biomes.json row has unknown key(s): %s" % sorted(extra))
        if "reachable" in r and not isinstance(r["reachable"], bool):
            raise SystemExit("biomes.json reachable must be a boolean")
        if r.get("reachable") is False and not r.get("unreachable_note"):
            raise SystemExit("a tombstoned biome must say why it is unreachable")
        if len(r["color"]) != 3:
            raise SystemExit("biome %s needs an RGB triple" % r["id"])
    if not any(r.get("reachable", True) for r in rows):
        raise SystemExit("biomes.json tombstoned every natural biome")

    names = doc["variant_names"]
    lengths = {len(v) for v in names.values()}
    if lengths != {len(rows)}:
        raise SystemExit("every school needs one variant name per natural biome, in catalogue order")
    return rows, names


def display_name(row):
    # Matches the reference: id 17 is special-cased, everything else is the core name
    # with underscores opened out and sentence case applied.
    if row["id"] == 17:
        return "Persistent land ice"
    core = row["core"].replace("_", " ")
    return core[:1].upper() + core[1:].lower()


def render_biomes():
    rows, names = load_biomes()
    schools = sorted(names)

    lines = [BANNER.format(source="biomes.json")]
    lines.append('#include "fwg/content.hpp"\n')
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// ORDER IS A CONTRACT. `biome_variant` stores a flat index into the cross")
    lines.append("// product of these rows with the magic schools, so inserting a row renumbers")
    lines.append("// every variant in every world ever saved. Append, never insert.")
    lines.append("constexpr NaturalBiome kNaturalBiomes[] = {")
    for r in rows:
        lines.append("    {%d, %s, %s, %s, {%d, %d, %d}, %s},"
                     % (r["id"], cpp_string(r["core"]), cpp_string(display_name(r)),
                        cpp_string("terrain.biome.%03d" % r["id"]),
                        r["color"][0], r["color"][1], r["color"][2],
                        "true" if r.get("reachable", True) else "false"))
    lines.append("};\n")

    lines.append("// One variant name per natural biome, in catalogue order, per school.")
    lines.append("constexpr std::string_view kSchoolNames[] = {")
    for s in schools:
        lines.append("    %s," % cpp_string(s))
    lines.append("};\n")
    lines.append("constexpr std::string_view kVariantNames[][%d] = {" % len(rows))
    for s in schools:
        entries = ", ".join(cpp_string(v) for v in names[s])
        lines.append("    {%s}," % entries)
    lines.append("};\n")
    lines.append("}  // namespace\n")

    lines.append("std::span<const NaturalBiome> natural_biomes() { return kNaturalBiomes; }\n")
    lines.append("""const NaturalBiome* natural_biome(int id) {
    for (const NaturalBiome& row : kNaturalBiomes) {
        if (row.id == id) return &row;
    }
    return nullptr;
}
""")
    lines.append("std::span<const std::string_view> magic_schools() { return kSchoolNames; }\n")
    lines.append("""std::string_view variant_name(std::size_t school_index, std::size_t biome_index) {
    return kVariantNames[school_index][biome_index];
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def load_world_options():
    doc = json.loads((DATA / "world-options.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("world-options.json must declare schema version 1")
    opts = doc["options"]
    for name, spec in opts.items():
        if not name.replace("_", "").isalnum() or name[0].isdigit():
            raise SystemExit("option name %r is not a C++ identifier" % name)
        if spec["min"] > spec["max"]:
            raise SystemExit("option %s has min above max" % name)
        if not (spec["min"] <= spec["default"] <= spec["max"]):
            raise SystemExit("option %s default is outside its own bounds" % name)
        if spec["type"] not in ("number", "integer"):
            raise SystemExit("option %s has an unknown type %r" % (name, spec["type"]))
    return opts


def cpp_double(v):
    # Round-trip exactly, and keep it a double literal rather than an int.
    text = repr(float(v))
    return text if ("." in text or "e" in text or "E" in text or "inf" in text or "nan" in text) else text + ".0"


def render_world_options():
    opts = load_world_options()
    names = sorted(opts)

    lines = [BANNER.format(source="world-options.json")]
    lines.append('#include "fwg/options.hpp"\n')
    lines.append("#include <algorithm>\n")
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// Sorted by name so option_by_name can binary-search it.")
    lines.append("constexpr OptionSpec kOptions[] = {")
    for name in names:
        o = opts[name]
        lines.append('    {%s, %s, %s, %s, %s, %s, %s},'
                     % (cpp_string(name), cpp_double(o["default"]), cpp_double(o["min"]),
                        cpp_double(o["max"]), "true" if o["type"] == "integer" else "false",
                        cpp_string(o["units"]), cpp_string(o["group"])))
    lines.append("};\n")
    lines.append("static_assert(sizeof(kOptions) / sizeof(kOptions[0]) == kOptionCount,")
    lines.append('              "options.hpp enum and the generated table disagree");\n')
    lines.append("}  // namespace\n")
    lines.append("std::span<const OptionSpec> option_specs() { return kOptions; }\n")
    lines.append("""const OptionSpec& option_spec(Option option) {
    return kOptions[static_cast<std::size_t>(option)];
}
""")
    lines.append("""std::optional<Option> option_by_name(std::string_view name) {
    const auto* first = std::begin(kOptions);
    const auto* last = std::end(kOptions);
    const auto* it = std::lower_bound(first, last, name,
                                      [](const OptionSpec& spec, std::string_view key) { return spec.name < key; });
    if (it == last || it->name != name) return std::nullopt;
    return static_cast<Option>(it - first);
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def render_options_header():
    opts = load_world_options()
    names = sorted(opts)
    lines = [BANNER.format(source="world-options.json").replace("// GENERATED FILE", "// GENERATED ENUM")]
    lines.append("#pragma once\n")
    lines.append("namespace fwg {\n")
    lines.append("// One entry per world option, in the same order as the generated table.")
    lines.append("enum class Option : int {")
    for i, name in enumerate(names):
        lines.append("    %s = %d," % (name, i))
    lines.append("};\n")
    lines.append("inline constexpr int kOptionCount = %d;\n" % len(names))
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def load_schools():
    doc = json.loads((DATA / "magic-schools.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("magic-schools.json must declare schema version 1")
    rows = doc["schools"]
    names = [r["name"] for r in rows]
    if len(set(names)) != len(names):
        raise SystemExit("magic-schools.json has a duplicate school name")
    # Position is a contract, and hidden schools are appended. A hidden school before a
    # known one would renumber dominant_magic in every world ever saved.
    seen_hidden = False
    for r in rows:
        if r["hidden"]:
            seen_hidden = True
        elif seen_hidden:
            raise SystemExit("a known school follows a hidden one: hidden schools must be appended")
        if len(r["color"]) != 3:
            raise SystemExit("school %s needs an RGB triple" % r["name"])
    if not any(not r["hidden"] for r in rows):
        raise SystemExit("magic-schools.json hid every school")
    return rows


def render_schools():
    rows = load_schools()
    lines = [BANNER.format(source="magic-schools.json")]
    lines.append('#include "fwg/magic.hpp"\n')
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// ORDER IS A CONTRACT. dominant_magic exports an index into this table.")
    lines.append("constexpr MagicSchool kSchools[] = {")
    for r in rows:
        lines.append("    {%s, %s, %s, {%d, %d, %d}, %s},"
                     % (cpp_string(r["name"]), cpp_string(r["group"]), cpp_string(r["descriptors"]),
                        r["color"][0], r["color"][1], r["color"][2],
                        "true" if r["hidden"] else "false"))
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::span<const MagicSchool> magic_school_table() { return kSchools; }\n")
    lines.append("""int magic_school_index(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kSchools); ++i) {
        if (kSchools[i].name == name) return static_cast<int>(i);
    }
    return -1;
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def load_registry_sections():
    doc = json.loads((DATA / "registry-sections.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("registry-sections.json must declare schema version 1")

    races = doc["parent_races"]
    ids = [r["id"] for r in races]
    if len(set(ids)) != len(ids):
        raise SystemExit("registry-sections.json has a duplicate parent race id")
    if not races:
        raise SystemExit("registry-sections.json has no parent race; nobody could found a city")
    for r in races:
        pct = r["founding_participation_percent"]
        if not 0 <= pct <= 100:
            raise SystemExit("parent race %s participation %r is not a percentage" % (r["id"], pct))

    rules = doc["founding_rules"]
    # The founding clock only ever lands on multiples of the round length, so an interval
    # that is not a multiple is tested with `year % interval == 0` on no round at all: no
    # diaspora, no error, and every people that needed the bonus quietly missing from the
    # map. Caught here rather than at runtime, because the data is what would be wrong.
    if rules["years_per_round"] <= 0:
        raise SystemExit("years_per_round must be positive")
    if rules["diaspora_interval_years"] % rules["years_per_round"] != 0:
        raise SystemExit("diaspora_interval_years %r is not a multiple of years_per_round %r; "
                         "it would silently never fire"
                         % (rules["diaspora_interval_years"], rules["years_per_round"]))
    if rules["diaspora_bonus_per_parent"] not in (0, 1):
        raise SystemExit("diaspora_bonus_per_parent is a flag, not a count")
    if not 0 <= rules["origin_min_separation_degrees"] <= 180:
        raise SystemExit("origin_min_separation_degrees must be 0..180")
    return doc


def render_registry_sections():
    doc = load_registry_sections()
    races = doc["parent_races"]
    rules = doc["founding_rules"]
    defaults = doc["defaults"]
    classification = doc["city_classification"]

    lines = [BANNER.format(source="registry-sections.json")]
    lines.append('#include "fwg/founding.hpp"\n')
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// ORDER IS A CONTRACT. Founding rotates initiative through this table by turn,")
    lines.append("// so reordering it changes which parent claims a contested site first.")
    lines.append("constexpr ParentRace kParentRaces[] = {")
    for r in races:
        lines.append("    {%s, %s, %s},"
                     % (cpp_string(r["id"]), cpp_string(r["name"]),
                        cpp_double(r["founding_participation_percent"])))
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::span<const ParentRace> parent_races() { return kParentRaces; }\n")

    lines.append("FoundingRules authored_founding_rules() {")
    lines.append("    FoundingRules r;")
    lines.append("    r.years_per_round = %d;" % rules["years_per_round"])
    lines.append("    r.origin_min_separation_degrees = %s;"
                 % cpp_double(rules["origin_min_separation_degrees"]))
    lines.append("    r.diaspora_interval_years = %d;" % rules["diaspora_interval_years"])
    lines.append("    r.diaspora_bonus_per_parent = %d;" % rules["diaspora_bonus_per_parent"])
    lines.append("    return r;")
    lines.append("}\n")

    lines.append("RegistryDefaults registry_defaults() {")
    lines.append("    RegistryDefaults d;")
    lines.append("    d.profile_id = %s;" % cpp_string(defaults["profile_id"]))
    lines.append("    d.aggregate_profile_id = %s;" % cpp_string(defaults["aggregate_profile_id"]))
    lines.append("    d.resource_score_factor_label = %s;"
                 % cpp_string(defaults["resource_score_factor_label"]))
    lines.append("    d.normal_site_reason = %s;" % cpp_string(defaults["normal_site_reason"]))
    lines.append("    d.medium_suitability_min = %s;"
                 % cpp_double(classification["medium_suitability_min"]))
    lines.append("    return d;")
    lines.append("}\n")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


HABITAT_FIELDS = ("biome", "variant", "temperature", "moisture", "maritime", "landmass_area_m2",
                  "landmass_fraction", "largest_landmass", "resource", "slope", "height", "tpi",
                  "coastal_support", "abs_latitude")


def load_civilizations():
    doc = json.loads((DATA / "civilizations.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("civilizations.json must declare schema version 1")
    rows = doc["profiles"]
    ids = [r["id"] for r in rows]
    if len(set(ids)) != len(ids):
        raise SystemExit("civilizations.json has a duplicate profile id")
    if doc["default_profile_id"] not in ids:
        raise SystemExit("the default profile is not in the registry")
    if not any(r["kind"] == "entity" for r in rows):
        raise SystemExit("civilizations.json has no entity civilization to settle the world")
    expected = set(doc["numeric_traits"])
    for r in rows:
        missing = expected - set(r["traits"])
        extra = set(r["traits"]) - expected
        if missing or extra:
            raise SystemExit("profile %s trait mismatch: missing %s extra %s"
                             % (r["id"], sorted(missing), sorted(extra)))
        for k, v in r["traits"].items():
            if not isinstance(v, (int, float)) or isinstance(v, bool):
                raise SystemExit("profile %s trait %s is not numeric -- named fields cannot carry it"
                                 % (r["id"], k))
    return doc


def flatten_habitat(rule, nodes, numbers, texts, children, base):
    """Append `rule` and its subtree to `nodes`; return this node's index.

    Children are recorded as INDICES in a shared pool rather than as a contiguous
    range of nodes. A range is correct only while no child has children of its own --
    nest two levels and it sweeps in a grandchild. The data is one level deep today,
    which is exactly why this would have gone unnoticed.
    """
    index = len(nodes)
    nodes.append(None)  # reserve, so children get higher indices

    if not rule:
        nodes[index] = dict(op="always", field="none", child=(0, 0), num=(0, 0), txt=(0, 0))
        return index

    for op in ("all", "any"):
        if op in rule:
            kids = [flatten_habitat(c, nodes, numbers, texts, children, base) for c in rule[op]]
            begin = len(children)
            # RELATIVE to the profile's own first node. A profile's `habitat` span is a
            # slice of the shared array, so a global index would read another profile's
            # tree the moment a span did not start at zero.
            children.extend(k - base for k in kids)
            nodes[index] = dict(op=op, field="none", child=(begin, len(children)),
                                num=(0, 0), txt=(0, 0))
            return index

    field = rule["field"]
    if field not in HABITAT_FIELDS:
        raise SystemExit("unknown habitat field %r" % field)
    num = txt = (0, 0)
    if "in" in rule:
        values = rule["in"]
        if all(isinstance(v, str) for v in values):
            txt = (len(texts), len(texts) + len(values))
            texts.extend(values)
        else:
            num = (len(numbers), len(numbers) + len(values))
            numbers.extend(float(v) for v in values)
    nodes[index] = dict(op="leaf", field=field, child=(0, 0), num=num, txt=txt,
                        min=rule.get("min"), max=rule.get("max"),
                        gt=rule.get("gt"), equals=rule.get("equals"))
    return index


def render_civilizations():
    doc = load_civilizations()
    rows = doc["profiles"]
    traits = doc["numeric_traits"]

    nodes, numbers, texts, children = [], [], [], []
    spans = []
    for r in rows:
        start = len(nodes)
        flatten_habitat(r["habitat"], nodes, numbers, texts, children, start)
        spans.append((start, len(nodes)))

    # Every child index must land inside its own profile's tree, counted from that
    # profile's first node.
    for (start, end), r in zip(spans, rows):
        size = end - start
        for n in nodes[start:end]:
            for c in children[n["child"][0]:n["child"][1]]:
                if not (0 <= c < size):
                    raise SystemExit("profile %s has a habitat child outside its own tree" % r["id"])

    lines = [BANNER.format(source="civilizations.json")]
    lines.append('#include "fwg/civilizations.hpp"\n')
    lines.append("#include <array>\n")
    lines.append("namespace fwg {\nnamespace {\n")

    lines.append("constexpr double kInNumbers[] = {%s};"
                 % (", ".join(cpp_double(v) for v in numbers) if numbers else "0.0"))
    lines.append("constexpr std::string_view kInTexts[] = {%s};"
                 % (", ".join(cpp_string(v) for v in texts) if texts else '""'))
    lines.append("constexpr std::uint32_t kHabitatChildren[] = {%s};\n"
                 % (", ".join("%du" % v for v in children) if children else "0u"))

    lines.append("// Habitat predicates, flattened. Children of a node are a contiguous range.")
    lines.append("constexpr HabitatNode kHabitat[] = {")
    for n in nodes:
        lines.append("    {HabitatOp::%s, HabitatField::%s, %s, %s, %s, %s, %s, %s, %s, %s, %du, %du, %du, %du, %du, %du},"
                     % (n["op"], n["field"],
                        cpp_double(n.get("min") or 0.0), cpp_double(n.get("max") or 0.0),
                        cpp_double(n.get("gt") or 0.0), cpp_double(n.get("equals") or 0.0),
                        "true" if n.get("min") is not None else "false",
                        "true" if n.get("max") is not None else "false",
                        "true" if n.get("gt") is not None else "false",
                        "true" if n.get("equals") is not None else "false",
                        n["num"][0], n["num"][1], n["txt"][0], n["txt"][1],
                        n["child"][0], n["child"][1]))
    lines.append("};\n")

    for key, kind in (("biome_preferences", "BiomeOpinion"), ("food_biome_multipliers", "BiomeOpinion"),
                      ("magic_biome_preferences", "VariantOpinion"),
                      ("food_magic_biome_multipliers", "VariantOpinion")):
        pool, offsets = [], []
        for r in rows:
            offsets.append(len(pool))
            for k in sorted(r[key]):
                pool.append((k, r[key][k]))
            offsets[-1] = (offsets[-1], len(pool))
        entries = []
        for k, v in pool:
            entries.append("{%s, %s}" % (int(k) if kind == "BiomeOpinion" else cpp_string(k), cpp_double(v)))
        lines.append("constexpr %s k_%s[] = {%s};" % (kind, key, ", ".join(entries) if entries else "{}"))
        globals()["_offsets_" + key] = offsets
    lines.append("")

    lines.append("constexpr CivilizationProfile kProfiles[] = {")
    for i, r in enumerate(rows):
        t = ", ".join(cpp_double(r["traits"][k]) for k in traits)
        spanish = []
        for key in ("biome_preferences", "food_biome_multipliers",
                    "magic_biome_preferences", "food_magic_biome_multipliers"):
            begin, end = globals()["_offsets_" + key][i]
            spanish.append("{k_%s + %d, %d}" % (key, begin, end - begin))
        lines.append("    {%s, %s, %s, %s, %s, %s, %s, %s, %d, {%s}, {kHabitat + %d, %d}, %s},"
                     % (cpp_string(r["id"]), cpp_string(r["name"]), cpp_string(r["description"]),
                        cpp_string(r["environment"]), cpp_string(r["inspiration"] or ""),
                        cpp_string(r["allocation_group"] or ""), cpp_string(r["structure_blocks"] or ""),
                        "true" if r["kind"] == "entity" else "false", r["priority"], t,
                        spans[i][0], spans[i][1] - spans[i][0], ", ".join(spanish)))
    lines.append("};\n")

    entity = [r["id"] for r in rows if r["kind"] == "entity"]
    lines.append("constexpr std::string_view kEntityIds[] = {%s};\n"
                 % ", ".join(cpp_string(v) for v in entity))

    src = doc["source_identity"]
    digest = hashlib.sha256(json.dumps(doc, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    lines.append("}  // namespace\n")
    lines.append("std::span<const std::uint32_t> habitat_children() { return kHabitatChildren; }")
    lines.append("std::span<const double> habitat_numbers() { return kInNumbers; }")
    lines.append("std::span<const std::string_view> habitat_texts() { return kInTexts; }")
    lines.append("std::span<const CivilizationProfile> civilization_profiles() { return kProfiles; }")
    lines.append("std::span<const std::string_view> civilization_ids() { return kEntityIds; }")
    lines.append("std::string_view default_profile_id() { return %s; }\n"
                 % cpp_string(doc["default_profile_id"]))
    lines.append("""RegistryIdentity registry_identity() {
    return {%d, %d, %s, %s};
}
""" % (src["schema_version"], src["revision"], cpp_string(src["sha256"]), cpp_string(digest)))
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def render_variants():
    biomes, names = load_biomes()
    schools = load_schools()
    known = [s for s in schools if not s["hidden"]]
    hidden = [s for s in schools if s["hidden"]]

    rows = []
    # Two passes, deliberately: the known block keeps its historical positions and the
    # hidden block is appended. A single nested loop would interleave them and renumber
    # every variant in every saved world.
    for block in (known, hidden):
        for i, b in enumerate(biomes):
            for s in block:
                rows.append({
                    "id": "%s.%s" % (b["core"], s["name"]),
                    "core": b["core"],
                    "core_biome_id": b["id"],
                    "school": s["name"],
                    "group": s["group"],
                    "name": names[s["name"]][i],
                    "asset_id": "terrain.mutation.%s.%s" % (b["core"], s["name"]),
                    "color": s["color"],
                    "hidden": s["hidden"],
                })

    expected = len(biomes) * len(schools)
    if len(rows) != expected:
        raise SystemExit("variant catalogue is %d rows, expected %d" % (len(rows), expected))
    if len({r["id"] for r in rows}) != len(rows):
        raise SystemExit("variant catalogue has a duplicate id")

    lines = [BANNER.format(source="biomes.json and magic-schools.json")]
    lines.append('#include "fwg/content.hpp"\n')
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// POSITION IS THE CONTRACT -- biome_variant is a flat index into this.")
    lines.append("constexpr BiomeVariant kVariants[] = {")
    for r in rows:
        lines.append("    {%s, %s, %d, %s, %s, %s, %s, {%d, %d, %d}, %s},"
                     % (cpp_string(r["id"]), cpp_string(r["core"]), r["core_biome_id"],
                        cpp_string(r["school"]), cpp_string(r["group"]), cpp_string(r["name"]),
                        cpp_string(r["asset_id"]), r["color"][0], r["color"][1], r["color"][2],
                        "true" if r["hidden"] else "false"))
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::span<const BiomeVariant> biome_variant_catalogue() { return kVariants; }\n")
    lines.append("""int biome_variant_index(std::string_view core, std::string_view school) {
    for (std::size_t i = 0; i < std::size(kVariants); ++i) {
        if (kVariants[i].core == core && kVariants[i].magic_school == school) return static_cast<int>(i);
    }
    return -1;
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {
    OUT / "content_biomes.cpp": render_biomes,
    OUT / "content_schools.cpp": render_schools,
    OUT / "content_civilizations.cpp": render_civilizations,
    OUT / "content_variants.cpp": render_variants,
    OUT / "content_registry.cpp": render_registry_sections,
    OUT / "world_options.cpp": render_world_options,
    pathlib.Path(__file__).resolve().parent.parent / "include" / "fwg" / "option_ids.hpp": render_options_header,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if the checked-in output is stale")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    stale = []
    for path, render in TARGETS.items():
        text = render()
        if args.check:
            current = path.read_text(encoding="utf-8") if path.exists() else ""
            if current != text:
                stale.append(path.relative_to(ROOT).as_posix())
        else:
            path.write_text(text, encoding="utf-8")
            print("wrote", path.relative_to(ROOT).as_posix())

    if stale:
        print("STALE, regenerate with tools/embed_content.py: " + ", ".join(stale), file=sys.stderr)
        return 1
    if args.check:
        print("generated content is current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
