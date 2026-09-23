"""The key-location catalogue: 97 archetypes, 11 families, 75 exterior parts, 102 kits.

Three documents render into one translation unit because they are one catalogue with
three faces -- what a place is, what moved into it, and what stands on the ground -- and
splitting them would let an archetype ship without the kit that draws it.

Four orderings reach a generated world and are preserved deliberately. They are the whole
reason this file validates rather than merely transcribes:

  * `archetypes[].names` is POSITIONAL. `name_for` draws `templates[randrange(len)]` and
    the two filtered sublists (`{ordinal}` templates, `{near}`-free templates) are built
    by a comprehension that preserves source order. Reordering one array renames every
    site of that archetype in every world.
  * `archetypes[].prefers` is POSITIONAL twice over: the suitability score is a float sum
    accumulated in list order, and `suitability_factors` is built in the same order with
    last-wins on a repeated layer key.
  * `archetypes[].requires` decides only the DIAGNOSTIC, not the outcome -- the gate is an
    all() over the terms -- because `resolve` and `gap` both return on the first offending
    term. Carried in file order for that reason alone.
  * A KIT's `parts` list is the hardest order contract in the catalogue: the per-site RNG
    stream is consumed entry by entry, so reordering one entry re-rolls every count,
    position and rotation after it, and it also fixes the positional `plot-<k>` ids and
    the order the non-associative shaping sum is accumulated in.

What is NOT an order contract, and is re-sorted at runtime rather than here: `states`,
`occupants` and the succession occupant maps, which `choose` sorts by key before its
cumulative walk. They are emitted in file order so that the runtime sort stays a testable
step rather than a property of the table.

`archetypes` file order itself is free -- the scatter pass re-sorts on (-tier, id), a
total order because ids are unique -- so the rows are emitted sorted by id, which also
lets a lookup be a binary search. `chains` and `clusters` are the opposite way round: free
for placement (both passes sort by id) and a contract for the diagnostics tail, which
walks them in DOCUMENT order, so those keep file order.
"""
from embed.common import BANNER, OUT, cpp_double, cpp_string, load

CATALOGUE = "key-locations-catalogue.json"
EXTERIORS = "key-location-exteriors.json"
CODE_TABLES = "key-locations-code-tables.json"

DOMAINS = ("land", "water", "ocean", "lake", "any")
PLACEMENTS = ("node", "path", "cluster")
ALONG = ("road", "frontier", "approach")
INTERIOR_PLANS = ("linear", "branching", "radial", "warren", "vault")
STATES = ("natural", "active", "occupied", "abandoned", "ruined", "buried", "drowned",
          "sealed", "corrupted", "reclaimed", "restored")
OCCUPANTS = ("none", "builders", "descendants", "squatters", "monsters", "cult", "undead",
             "beasts", "fae", "nature")
TERM_KEYS = ("layer", "min", "max", "equals", "not_equals", "in", "above_percentile",
             "below_percentile")
PREFER_KEYS = ("layer", "weight", "invert")
PART_KINDS = ("structure", "terrain", "opening", "path")
ARRANGEMENTS = ("single", "ring", "scatter", "row", "courtyard", "terrace", "mouth")
SHAPES = ("mound", "cut", "rim", "platform", "bank", "trench", "level")

ENUM = {name: name[:1].upper() + name[1:] for name in
        set(DOMAINS) | set(PLACEMENTS) | set(ALONG) | set(INTERIOR_PLANS) |
        set(PART_KINDS) | set(ARRANGEMENTS) | set(SHAPES)}


def revision_of(doc):
    """Provenance is the integer revision; there is no digest over these bytes anywhere."""
    identity = doc.get("source_identity") or {}
    revision = identity.get("revision")
    if type(revision) is not int or revision < 1:
        raise SystemExit("%s needs a positive integer source_identity.revision" % doc.get("schema"))
    return revision


# ---------------------------------------------------------------------------
# Validation. Mirrors key_locations/catalogue.py:lint and exteriors.py:lint, and is
# fail-closed for the same reason: a catalogue that has lost a gate is not a looser
# catalogue, it is one whose archetypes stand on ground their own reason misdescribes.
# ---------------------------------------------------------------------------

def lint_catalogue(doc):
    families = doc["families"]
    archetypes = doc["archetypes"]
    ids = set()
    for a in archetypes:
        for key in ("id", "name", "family", "tier", "domain", "prefers", "states",
                    "occupants", "names", "reason"):
            if key not in a:
                raise SystemExit("archetype %r lacks %s" % (a.get("id"), key))
        if a["id"] in ids:
            raise SystemExit("duplicate archetype id %r" % a["id"])
        ids.add(a["id"])
        if a["family"] not in families:
            raise SystemExit("archetype %r names unknown family %r" % (a["id"], a["family"]))
        if a["tier"] not in (0, 1, 2, 3):
            raise SystemExit("archetype %r has tier outside 0..3" % a["id"])
        if a["domain"] not in DOMAINS:
            raise SystemExit("archetype %r has unknown domain %r" % (a["id"], a["domain"]))
        placement = a.get("placement", "node")
        if placement not in PLACEMENTS:
            raise SystemExit("archetype %r has unknown placement %r" % (a["id"], placement))
        # A zero rate is the silent form of never: budget multiplies it by land area, so
        # it reports `wanted: 0` against any number of qualifying cells and nobody sees it.
        if placement == "node" and not (isinstance(a.get("per_1000_km2"), (int, float))
                                        and a["per_1000_km2"] > 0):
            raise SystemExit("node-placed archetype %r needs a positive per_1000_km2" % a["id"])
        for term in a.get("requires", []):
            extra = set(term) - set(TERM_KEYS)
            if extra or "layer" not in term or len(term) < 2:
                raise SystemExit("archetype %r has a malformed requires term %r" % (a["id"], term))
        for term in a["prefers"]:
            extra = set(term) - set(PREFER_KEYS)
            if extra or "layer" not in term:
                raise SystemExit("archetype %r has a malformed prefers term %r" % (a["id"], term))
        check_weights(a["states"], STATES, "archetype %r states" % a["id"])
        check_weights(a["occupants"], OCCUPANTS, "archetype %r occupants" % a["id"])
        if not a["names"] or not a["reason"]:
            raise SystemExit("archetype %r has no names or no reason" % a["id"])
        interior = a.get("interior")
        if interior is not None:
            if interior.get("plan") not in INTERIOR_PLANS:
                raise SystemExit("archetype %r has unknown interior plan" % a["id"])
            for key in ("levels", "chambers"):
                span = interior.get(key)
                if not (isinstance(span, list) and len(span) == 2 and 0 < span[0] <= span[1]):
                    raise SystemExit("archetype %r interior %s must be [lo, hi] with 0 < lo <= hi"
                                     % (a["id"], key))
        if a["tier"] == 2 and interior is None:
            raise SystemExit("tier-2 archetype %r has no interior" % a["id"])
        if a["tier"] == 0 and interior is not None:
            raise SystemExit("tier-0 archetype %r carries an interior" % a["id"])

    for family, rule in doc["succession"].items():
        # The succession map carries a top-level prose `note` beside the nine family
        # rules. Validating every key as a family refuses the shipped catalogue.
        if family == "note":
            continue
        if family not in families:
            raise SystemExit("succession names unknown family %r" % family)
        if not 0 <= rule.get("chance", -1) <= 1:
            raise SystemExit("succession %r has a chance outside [0, 1]" % family)
        check_weights(rule["occupants"], OCCUPANTS, "succession %r" % family)

    composed = set()
    for spec in doc["chains"]:
        for key in ("id", "name", "archetype", "along", "interval_m", "reason"):
            if key not in spec:
                raise SystemExit("chain %r lacks %s" % (spec.get("id"), key))
        if spec["archetype"] not in ids:
            raise SystemExit("chain %r names unknown archetype" % spec["id"])
        if spec["along"] not in ALONG:
            raise SystemExit("chain %r walks unknown thing %r" % (spec["id"], spec["along"]))
        if spec["interval_m"] <= 0:
            raise SystemExit("chain %r has a non-positive interval" % spec["id"])
        if spec["along"] == "approach" and not spec.get("anchor_family"):
            raise SystemExit("approach chain %r needs an anchor_family" % spec["id"])
        composed.add(spec["archetype"])
    for spec in doc["clusters"]:
        for key in ("id", "name", "archetype", "count", "offset_m", "reason"):
            if key not in spec:
                raise SystemExit("cluster %r lacks %s" % (spec.get("id"), key))
        if spec["archetype"] not in ids:
            raise SystemExit("cluster %r names unknown archetype" % spec["id"])
        if not spec.get("anchor_kinds") and not spec.get("anchor_families"):
            raise SystemExit("cluster %r anchors on nothing" % spec["id"])
        for key, length in (("count", 2), ("offset_m", 2)):
            span = spec[key]
            if not (isinstance(span, list) and len(span) == length and span[0] <= span[1]):
                raise SystemExit("cluster %r has a malformed %s" % (spec["id"], key))
        for kind in spec.get("anchor_kinds", []):
            if kind not in ids:
                raise SystemExit("cluster %r anchors on unknown archetype %r" % (spec["id"], kind))
        composed.add(spec["archetype"])
    by_id = {a["id"]: a for a in archetypes}
    for archetype_id in sorted(composed):
        a = by_id[archetype_id]
        # Otherwise a waystone arrives both strung along its road and dropped on whichever
        # cell scored best, by two passes that know nothing about each other.
        if a.get("placement", "node") == "node" and not a.get("also_scatters"):
            raise SystemExit("composed archetype %r is left at placement node without "
                             "also_scatters" % archetype_id)
    return archetypes, families


def check_weights(weights, vocabulary, owner):
    if not weights:
        raise SystemExit("%s is an empty weight map" % owner)
    for name, weight in weights.items():
        if name not in vocabulary:
            raise SystemExit("%s names unknown %r" % (owner, name))
        if not isinstance(weight, (int, float)) or weight <= 0:
            raise SystemExit("%s weights %r at %r, which is not positive" % (owner, name, weight))


def lint_exteriors(doc):
    parts = {}
    for part in doc["parts"]:
        if part["id"] in parts:
            raise SystemExit("duplicate part %r" % part["id"])
        if part.get("kind") not in PART_KINDS:
            raise SystemExit("part %r has unknown kind %r" % (part["id"], part.get("kind")))
        shape = part.get("shaping")
        if shape is not None:
            if shape.get("shape") not in SHAPES:
                raise SystemExit("part %r has unknown shape %r" % (part["id"], shape.get("shape")))
            if not isinstance(shape.get("radius_m") or shape.get("length_m"), (int, float)):
                raise SystemExit("part %r shapes the ground over no extent" % part["id"])
            # `level` flattens toward the centre height, so height_m 0 is meaningful there
            # and means "flat, no offset". Every other shape moving the ground by nothing
            # is a no-op wearing a shaping spec.
            if shape["shape"] != "level" and not any(
                    isinstance(shape.get(k), (int, float)) and shape[k] != 0
                    for k in ("height_m", "depth_m")):
                raise SystemExit("part %r shapes the ground by nothing" % part["id"])
        for box, fields in (("plot_m", ("width", "depth")),
                            ("dimensions_m", ("width", "depth", "height"))):
            for field in fields:
                value = part.get(box, {}).get(field)
                if not isinstance(value, (int, float)) or value <= 0:
                    raise SystemExit("part %r needs a positive %s.%s" % (part["id"], box, field))
        parts[part["id"]] = part
    for group in ("family_kits", "archetype_kits"):
        for key, kit in doc[group].items():
            if kit["arrangement"] not in ARRANGEMENTS:
                raise SystemExit("kit %r has unknown arrangement %r" % (key, kit["arrangement"]))
            if not kit.get("parts"):
                raise SystemExit("kit %r places nothing" % key)
            for entry in kit["parts"]:
                if entry["part"] not in parts:
                    raise SystemExit("kit %r names unknown part %r" % (key, entry["part"]))
                if entry.get("arrangement", kit["arrangement"]) not in ARRANGEMENTS:
                    raise SystemExit("kit %r has unknown arrangement %r" % (key, entry["arrangement"]))
                low, high = entry.get("count", [1, 1])
                # A count of 0 IS legal and means "sometimes none".
                if not 0 <= low <= high:
                    raise SystemExit("kit %r has a malformed count %r" % (key, entry.get("count")))
    return doc["parts"], doc["family_kits"], doc["archetype_kits"]


def check_kits_resolve(archetypes, families, family_kits, archetype_kits, parts):
    """Every archetype resolves a kit, and the weathering parts the code names exist.

    `kit_for` raises when neither lookup hits, which would turn one archetype into a
    failed plans block at runtime. Nothing in the Python validates the two hardcoded
    weathering part ids at all, so they are checked here.
    """
    names = {p["id"] for p in parts}
    for a in archetypes:
        if a["id"] not in archetype_kits and a["family"] not in family_kits:
            raise SystemExit("no exterior kit for %r and no family kit for %r"
                             % (a["id"], a["family"]))
    for part_id in ("keyloc.part.rubble_pile", "keyloc.part.overgrown_mound"):
        if part_id not in names:
            raise SystemExit("the weathering pass names part %r, which the catalogue "
                             "does not carry" % part_id)
    for family in families:
        if family not in family_kits:
            raise SystemExit("family %r has no family kit" % family)


# ---------------------------------------------------------------------------
# Rendering.
# ---------------------------------------------------------------------------

def weight_rows(weights):
    """A weight map in FILE order. `choose` sorts at runtime; see the module docstring."""
    return [(name, float(value)) for name, value in weights.items()]


def render_key_locations():
    catalogue = load(CATALOGUE, 1)
    exteriors = load(EXTERIORS, 1)
    code = load(CODE_TABLES, 1)
    archetypes, families = lint_catalogue(catalogue)
    parts, family_kits, archetype_kits = lint_exteriors(exteriors)
    check_kits_resolve(archetypes, families, family_kits, archetype_kits, parts)

    ordered = sorted(archetypes, key=lambda a: a["id"])
    part_index = {p["id"]: i for i, p in enumerate(parts)}

    requires, prefers, weights, names, texts, doubles, entries = [], [], [], [], [], [], []
    spans = {}
    for a in ordered:
        row = {}
        row["requires"] = (len(requires), len(a.get("requires", [])))
        for term in a.get("requires", []):
            in_span = (len(doubles), len(term.get("in", [])))
            doubles.extend(float(v) for v in term.get("in", []))
            requires.append((term, in_span))
        row["prefers"] = (len(prefers), len(a["prefers"]))
        prefers.extend(a["prefers"])
        row["states"] = (len(weights), len(a["states"]))
        weights.extend(weight_rows(a["states"]))
        row["occupants"] = (len(weights), len(a["occupants"]))
        weights.extend(weight_rows(a["occupants"]))
        row["names"] = (len(names), len(a["names"]))
        names.extend(a["names"])
        spans[a["id"]] = row

    succession_rows = []
    for family, rule in catalogue["succession"].items():
        if family == "note":
            continue
        begin = len(weights)
        weights.extend(weight_rows(rule["occupants"]))
        succession_rows.append((family, rule, begin, len(weights) - begin))

    lines = [BANNER.format(source="%s, %s and %s" % (CATALOGUE, EXTERIORS, CODE_TABLES))]
    lines.append('#include "fwg/key_locations.hpp"\n')
    lines.append("namespace fwg {")
    lines.append("namespace {\n")

    lines.append("// Every `in` list any requires term carries. No shipped term uses one; the")
    lines.append("// array exists so that adding one is a catalogue edit rather than a code change.")
    lines.append("constexpr double kInValues[] = {%s 0.0};"
                 % "".join("%s," % cpp_double(v) for v in doubles))
    lines.append("")

    lines.append("// FILE ORDER. The gate is an all() over these, so the order does not decide")
    lines.append("// which cells pass -- but `resolve` and `gap` both return on the FIRST")
    lines.append("// offending term, so it decides which layer the exported diagnostic names.")
    lines.append("constexpr KeyLocationRequire kRequires[] = {")
    for term, in_span in requires:
        lines.append("    {%s, %s, %s, %s, %s, %s, %s, %s, %s, {kInValues + %d, %d}},"
                     % (cpp_string(term["layer"]),
                        flag_value(term, "min"), flag_value(term, "max"),
                        flag_value(term, "equals"), flag_value(term, "not_equals"),
                        flag_value(term, "above_percentile"), flag_value(term, "below_percentile"),
                        "true" if "in" in term else "false",
                        "true" if ("above_percentile" in term or "below_percentile" in term) else "false",
                        in_span[0], in_span[1]))
    lines.append("};\n")

    lines.append("// FILE ORDER, twice load-bearing: the suitability score is a float sum")
    lines.append("// accumulated over these in order, and `suitability_factors` is built in the")
    lines.append("// same order with last-wins on a repeated layer key.")
    lines.append("constexpr KeyLocationPrefer kPrefers[] = {")
    for term in prefers:
        lines.append("    {%s, %s, %s},"
                     % (cpp_string(term["layer"]), cpp_double(term.get("weight", 1.0)),
                        "true" if term.get("invert") else "false"))
    lines.append("};\n")

    lines.append("// State, occupant and succession-occupant weights, in FILE order. `choose`")
    lines.append("// sorts by name before its cumulative walk, so that sort is a runtime step a")
    lines.append("// test can remove and see fail rather than a property baked in here.")
    lines.append("constexpr KeyLocationWeight kWeights[] = {")
    for name, value in weights:
        lines.append("    {%s, %s}," % (cpp_string(name), cpp_double(value)))
    lines.append("};\n")

    lines.append("// POSITIONAL: `name_for` draws templates[randrange(len)], and the two filtered")
    lines.append("// sublists preserve this order. Reordering one array renames every site of that")
    lines.append("// archetype in every world.")
    lines.append("constexpr std::string_view kNameTemplates[] = {")
    for template in names:
        lines.append("    %s," % cpp_string(template))
    lines.append("};\n")

    lines.append("// The families, in DOCUMENT order: the block republishes this map whole, so the")
    lines.append("// order survives into the world file and into the lab's map legend.")
    lines.append("constexpr KeyLocationFamily kFamilies[] = {")
    for key, family in families.items():
        lines.append("    {%s, %s, %s, {%d, %d, %d}, %s},"
                     % (cpp_string(key), cpp_string(family["name"]), cpp_string(family["glyph"]),
                        family["color"][0], family["color"][1], family["color"][2],
                        cpp_double(family["family_spacing_cells"])))
    lines.append("};\n")

    lines.append("// Sorted by id. File order is free here: the scatter pass re-sorts on")
    lines.append("// (-tier, id), which is total because ids are unique, so there is no fallback")
    lines.append("// to input order. That sorted order IS load-bearing -- used_names, claimed and")
    lines.append("// family_taken are shared mutable state threaded through it -- but the contract")
    lines.append("// is (-tier, id) and not this array.")
    lines.append("constexpr KeyLocationArchetype kArchetypes[] = {")
    for a in ordered:
        row = spans[a["id"]]
        interior = a.get("interior")
        lines.append("    {%s, %s, %s, %d, KeyLocationDomain::%s, KeyLocationPlacement::%s, %s,"
                     % (cpp_string(a["id"]), cpp_string(a["name"]), cpp_string(a["family"]),
                        a["tier"], ENUM[a["domain"]], ENUM[a.get("placement", "node")],
                        "true" if a.get("also_scatters") else "false"))
        lines.append("     {kRequires + %d, %d}, {kPrefers + %d, %d}, {kWeights + %d, %d}, "
                     "{kWeights + %d, %d}, {kNameTemplates + %d, %d},"
                     % (row["requires"][0], row["requires"][1],
                        row["prefers"][0], row["prefers"][1],
                        row["states"][0], row["states"][1],
                        row["occupants"][0], row["occupants"][1],
                        row["names"][0], row["names"][1]))
        lines.append("     %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s,"
                     % (cpp_double(a.get("per_1000_km2", 0.0)),
                        cpp_double(a.get("occurrence", 1.0)),
                        optional_number(a, "spacing_m"),
                        cpp_double(a.get("spacing_cells", 0.0)),
                        optional_number(a, "settlement_clearance_m"),
                        cpp_double(a.get("settlement_clearance_cells", 0.0)),
                        "true" if "min_count" in a else "false", int(a.get("min_count", 0)),
                        "true" if "max_count" in a else "false", int(a.get("max_count", 0)),
                        "true" if interior else "false",
                        ("KeyLocationPlan::%s" % ENUM[interior["plan"]]) if interior
                        else "KeyLocationPlan::Linear"))
        lines.append("     %d, %d, %d, %d, %s, %s},"
                     % (interior["levels"][0] if interior else 0,
                        interior["levels"][1] if interior else 0,
                        interior["chambers"][0] if interior else 0,
                        interior["chambers"][1] if interior else 0,
                        cpp_string(a["reason"]), cpp_string(a.get("note", ""))))
    lines.append("};\n")

    lines.append("// Nine of the eleven families. `lair` and `wonder` have no rule at all: nothing")
    lines.append("// moves into them, and an absent rule is that statement rather than a zero chance.")
    lines.append("constexpr KeyLocationSuccessionRule kSuccession[] = {")
    for family, rule, begin, count in succession_rows:
        lines.append("    {%s, %s, {kWeights + %d, %d}, %s},"
                     % (cpp_string(family), cpp_double(rule["chance"]), begin, count,
                        cpp_string(rule.get("note", ""))))
    lines.append("};\n")

    anchor_texts = []
    cluster_rows = []
    for spec in catalogue["clusters"]:
        row = {}
        for key in ("anchor_kinds", "anchor_families", "anchor_states"):
            begin = len(anchor_texts)
            anchor_texts.extend(spec.get(key, []))
            row[key] = (begin, len(anchor_texts) - begin)
        cluster_rows.append((spec, row))
    lines.append("constexpr std::string_view kAnchorTexts[] = {%s};"
                 % "".join("%s, " % cpp_string(t) for t in anchor_texts))
    lines.append("")

    lines.append("// DOCUMENT order. Both composition passes iterate specs sorted by id, so this")
    lines.append("// order does not place anything -- but the diagnostics tail walks chains then")
    lines.append("// clusters in raw order, and that tail is exported.")
    lines.append("constexpr KeyLocationChainSpec kChains[] = {")
    for spec in catalogue["chains"]:
        lines.append("    {%s, %s, %s, KeyLocationAlong::%s, %s, %s, %s, %d, %s, %d, %s},"
                     % (cpp_string(spec["id"]), cpp_string(spec["name"]),
                        cpp_string(spec["archetype"]), ENUM[spec["along"]],
                        cpp_double(spec["interval_m"]),
                        "true" if spec.get("line_of_sight") else "false",
                        cpp_string(spec.get("anchor_family", "")),
                        int(spec.get("anchor_tier", 2)),
                        "true" if "max_chains" in spec else "false",
                        int(spec.get("max_chains", 0)),
                        cpp_string(spec["reason"])))
    lines.append("};\n")

    lines.append("constexpr KeyLocationClusterSpec kClusters[] = {")
    for spec, row in cluster_rows:
        lines.append("    {%s, %s, %s, {kAnchorTexts + %d, %d}, {kAnchorTexts + %d, %d}, "
                     "{kAnchorTexts + %d, %d}, %d, %d, %s, %s, %s, %s},"
                     % (cpp_string(spec["id"]), cpp_string(spec["name"]),
                        cpp_string(spec["archetype"]),
                        row["anchor_kinds"][0], row["anchor_kinds"][1],
                        row["anchor_families"][0], row["anchor_families"][1],
                        row["anchor_states"][0], row["anchor_states"][1],
                        int(spec["count"][0]), int(spec["count"][1]),
                        cpp_double(spec["offset_m"][0]), cpp_double(spec["offset_m"][1]),
                        cpp_double(spec.get("chance", 1.0)), cpp_string(spec["reason"])))
    lines.append("};\n")

    lines.extend(render_exteriors(parts, family_kits, archetype_kits, part_index))
    lines.extend(render_code_tables(code))

    lines.append("}  // namespace\n")
    lines.append("std::span<const KeyLocationArchetype> key_location_archetypes() { return kArchetypes; }")
    lines.append("std::span<const KeyLocationFamily> key_location_families() { return kFamilies; }")
    lines.append("std::span<const KeyLocationSuccessionRule> key_location_succession() { return kSuccession; }")
    lines.append("std::span<const KeyLocationChainSpec> key_location_chain_specs() { return kChains; }")
    lines.append("std::span<const KeyLocationClusterSpec> key_location_cluster_specs() { return kClusters; }")
    lines.append("std::span<const KeyLocationPart> key_location_parts() { return kParts; }")
    lines.append("std::span<const KeyLocationKit> key_location_family_kits() { return kFamilyKits; }")
    lines.append("std::span<const KeyLocationKit> key_location_archetype_kits() { return kArchetypeKits; }")
    lines.append("std::span<const KeyLocationRoleSet> key_location_chamber_roles() { return kRoleSets; }")
    lines.append("std::span<const std::string_view> key_location_fallback_roles() { return kFallbackRoles; }")
    lines.append("std::span<const KeyLocationEntranceKind> key_location_entrance_kinds() { return kEntranceKinds; }")
    lines.append("std::span<const std::string_view> key_location_adjectives() { return kAdjectives; }")
    lines.append("std::span<const std::string_view> key_location_qualifiers() { return kQualifiers; }")
    lines.append("std::span<const std::string_view> key_location_ordinals() { return kOrdinals; }")
    lines.append("std::span<const KeyLocationThreatBase> key_location_threat_base() { return kThreatBase; }")
    lines.append("std::string_view key_location_plan_note(KeyLocationPlan plan) {")
    lines.append("    return kPlanNotes[static_cast<int>(plan)];")
    lines.append("}")
    lines.append("int key_location_catalogue_revision() { return %d; }" % revision_of(catalogue))
    lines.append("int key_location_exteriors_revision() { return %d; }" % revision_of(exteriors))
    lines.append("std::string_view key_location_method() { return %s; }" % cpp_string(code["method"]))
    lines.append("std::string_view key_location_limits() { return %s; }" % cpp_string(code["limits"]))
    lines.append("std::string_view key_location_plans_method() { return %s; }"
                 % cpp_string(code["exteriors"]["method"]))
    lines.append("std::string_view key_location_plans_limits() { return %s; }"
                 % cpp_string(code["exteriors"]["limits"]))
    lines.append("std::string_view key_location_interior_method() { return %s; }"
                 % cpp_string(code["interiors"]["method"]))
    lines.append("std::string_view key_location_interior_limits() { return %s; }"
                 % cpp_string("A graph, not a layout. No metre positions, no contents, no encounters."))
    lines.append("")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def flag_value(term, key):
    """`(has, value)` as two C++ initialisers.

    A missing bound is UNBOUNDED on that side and `min: 0` is a live constraint that
    rejects negatives, so the two cannot share a sentinel.
    """
    if key in term:
        return "true, %s" % cpp_double(term[key])
    return "false, 0.0"


def optional_number(row, key):
    return "true, %s" % cpp_double(row[key]) if key in row else "false, 0.0"


def render_exteriors(parts, family_kits, archetype_kits, part_index):
    lines = []
    lines.append("constexpr KeyLocationPart kParts[] = {")
    for p in parts:
        shape = p.get("shaping")
        lines.append("    {%s, %s, KeyLocationPartKind::%s, %s, %s, %s, %s, %s, %s},"
                     % (cpp_string(p["id"]), cpp_string(p["name"]), ENUM[p["kind"]],
                        cpp_double(p["plot_m"]["width"]), cpp_double(p["plot_m"]["depth"]),
                        cpp_double(p["dimensions_m"]["width"]), cpp_double(p["dimensions_m"]["depth"]),
                        cpp_double(p["dimensions_m"]["height"]),
                        shaping_literal(shape)))
    lines.append("};\n")

    kit_entries = []
    kit_spans = {}
    for group, table in (("family", family_kits), ("archetype", archetype_kits)):
        for key, kit in table.items():
            begin = len(kit_entries)
            for entry in kit["parts"]:
                kit_entries.append(entry)
            kit_spans[(group, key)] = (begin, len(kit_entries) - begin)

    lines.append("// THE HARDEST ORDER CONTRACT IN THE CATALOGUE. The per-site stream is consumed")
    lines.append("// entry by entry -- one randint for the count, then the arrangement's own draws")
    lines.append("// -- so moving one entry re-rolls every count, position and rotation after it.")
    lines.append("// The positional plot-<k> ids and the order the non-associative shaping sum is")
    lines.append("// accumulated in ride on the same order.")
    lines.append("constexpr KeyLocationKitEntry kKitEntries[] = {")
    for entry in kit_entries:
        low, high = entry.get("count", [1, 1])
        lines.append("    {%d, %d, %d, %s, %s},"
                     % (part_index[entry["part"]], int(low), int(high),
                        "true" if "arrangement" in entry else "false",
                        "KeyLocationArrangement::%s" % ENUM[entry.get("arrangement", "single")]))
    lines.append("};\n")

    for group, table in (("family", family_kits), ("archetype", archetype_kits)):
        lines.append("// Keyed by the BARE %s name, never by the kit's own `id`: three archetype"
                     % ("family" if group == "family" else "archetype"))
        lines.append("// kits share the id `mine_head`, so kit ids are not unique and cannot key a table.")
        lines.append("constexpr KeyLocationKit k%sKits[] = {" % group.capitalize())
        for key, kit in table.items():
            begin, count = kit_spans[(group, key)]
            lines.append("    {%s, %s, KeyLocationArrangement::%s, %s, %s, {kKitEntries + %d, %d}},"
                         % (cpp_string(key), cpp_string(kit["id"]), ENUM[kit["arrangement"]],
                            "true" if "spread_m" in kit else "false",
                            cpp_double(kit.get("spread_m", 0.0)), begin, count))
        lines.append("};\n")
    return lines


def shaping_literal(shape):
    if shape is None:
        return "{false, KeyLocationShape::Mound, false, 0.0, false, 0.0, false, 0.0, 0.0, 0.0}"
    return ("{true, KeyLocationShape::%s, %s, %s, %s, %s, %s, %s, %s, %s}"
            % (ENUM[shape["shape"]],
               "true" if "radius_m" in shape else "false", cpp_double(shape.get("radius_m", 0.0)),
               "true" if "length_m" in shape else "false", cpp_double(shape.get("length_m", 0.0)),
               "true" if "width_m" in shape else "false", cpp_double(shape.get("width_m", 0.0)),
               cpp_double(shape.get("height_m", 0.0)), cpp_double(shape.get("depth_m", 0.0))))


def render_code_tables(code):
    lines = []
    interiors = code["interiors"]
    roles, role_spans = [], {}
    for family, table in interiors["roles"].items():
        begin = len(roles)
        roles.extend(table)
        role_spans[family] = (begin, len(roles) - begin)
    lines.append("// The chamber-role tuples are NOT all the same length -- drowned and curiosity")
    lines.append("// carry five, wonder and wayside four -- and the draw is roles[randrange(len)],")
    lines.append("// so a fixed-six port would desynchronise the stream as well as inventing roles.")
    lines.append("constexpr std::string_view kChamberRoles[] = {")
    for role in roles:
        lines.append("    %s," % cpp_string(role))
    lines.append("};\n")
    lines.append("constexpr KeyLocationRoleSet kRoleSets[] = {")
    for family, (begin, count) in role_spans.items():
        lines.append("    {%s, {kChamberRoles + %d, %d}}," % (cpp_string(family), begin, count))
    lines.append("};\n")
    lines.append("constexpr std::string_view kFallbackRoles[] = {%s};"
                 % "".join("%s, " % cpp_string(r) for r in interiors["fallback_roles"]))
    lines.append("")
    lines.append("// Keyed by FAMILY despite the source document's key name: the code reads")
    lines.append("// ENTRANCE_KINDS.get(family, DEFAULT_ENTRANCE), and `funerary` and `lair` are")
    lines.append("// families rather than interior plans.")
    lines.append("constexpr KeyLocationEntranceKind kEntranceKinds[] = {")
    for family, kind in interiors["entrance_kinds_by_plan"].items():
        lines.append("    {%s, %s}," % (cpp_string(family), cpp_string(kind)))
    lines.append("};\n")
    notes = interiors["plan_notes"]
    lines.append("// Parallel to KeyLocationPlan, so an unnoted plan cannot be represented.")
    lines.append("constexpr std::string_view kPlanNotes[] = {%s};"
                 % "".join("%s, " % cpp_string(notes[plan]) for plan in INTERIOR_PLANS))
    lines.append("")
    naming = code["naming"]
    if set(naming["adjectives"]) & set(naming["qualifiers"]):
        raise SystemExit("adjectives and qualifiers overlap; the disambiguation pool shrinks "
                         "silently and only shows as a duplicate name on a crowded world")
    lines.append("// POSITIONAL: every {adj} is ADJECTIVES[randrange(24)], and the qualifier list")
    lines.append("// is walked in order to build disambiguating variants.")
    lines.append("constexpr std::string_view kAdjectives[] = {%s};"
                 % "".join("%s, " % cpp_string(a) for a in naming["adjectives"]))
    lines.append("constexpr std::string_view kQualifiers[] = {%s};"
                 % "".join("%s, " % cpp_string(q) for q in naming["qualifiers"]))
    lines.append("constexpr std::string_view kOrdinals[] = {%s};"
                 % "".join("%s, " % cpp_string(o) for o in code["composition"]["ordinals"]))
    lines.append("")
    base = code["succession"]["threat_base"]
    for occupant in OCCUPANTS:
        if occupant not in base:
            raise SystemExit("threat_base has no entry for occupant %r; the lookup defaults to "
                             "1 and a missing entry is indistinguishable from a harmless one"
                             % occupant)
    lines.append("constexpr KeyLocationThreatBase kThreatBase[] = {%s};"
                 % "".join("{%s, %d}, " % (cpp_string(k), int(v)) for k, v in base.items()))
    lines.append("")
    return lines


TARGETS = {OUT / "content_key_locations.cpp": render_key_locations}
