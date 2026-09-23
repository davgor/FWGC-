"""data/city-shapes.json -> src/generated/content_city_shapes.cpp.

THE WHOLE DOCUMENT IS AN RNG INPUT. select_shape salts every per-city draw with
sha256 over the catalogue's canonical JSON, so the citations in `sources`, the
`research_date`, the `status` and the prose in `layout` all decide which shape every
city in every world is given. That is why this module embeds the canonical BYTES and
lets C++ hash them at runtime instead of copying the digest across: a recorded digest
agrees with the data only until somebody edits the data, and then agrees with nothing.

The catalogue's list order is NOT a contract -- _rank() walks sorted(shapes, key=id)
and variation parameters are drawn in sorted key order -- so both sorts happen here
and the generated table is already in draw order. Doing it at runtime would mean
every consumer had to remember to.
"""
import hashlib
import json

from embed.common import BANNER, DATA, OUT, cpp_double, cpp_string

# The closed vocabulary of a site description. _rank raises on a site key outside it,
# so it is a validation table rather than documentation, and the C++ enum has to carry
# every name even though the planner supplies only seven of them.
FEATURES = ("coast_access", "navigable_water", "river_access", "river_bend", "bridge_feasible",
            "island", "peninsula", "lagoon", "ridge", "hill", "oasis",
            "usable_land_fraction", "corridor_constraint", "aridity", "wooded_fraction",
            "defense_priority", "regional_threat", "planned_foundation", "trade_intensity",
            "local_slope_degrees", "buildable_area_m2")

CLASSES = ("small", "medium", "capital")


def _canonical(catalogue):
    """The exact bytes city_shapes.select_shape hashes.

    sort_keys=True, the tightest separators, and ensure_ascii left at its default --
    all three are load-bearing, because the digest is an RNG input and not a checksum.
    """
    return json.dumps(catalogue, sort_keys=True, separators=(",", ":"))


def load_city_shapes():
    doc = json.loads((DATA / "city-shapes.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("city-shapes.json must declare schema version 1")
    catalogue = doc["catalogue"]

    if catalogue["schema"] != "fantasy-world-generator.city-shapes" or catalogue["schema_version"] != 1:
        raise SystemExit("unsupported city-shape catalogue schema")
    selection = catalogue["selection"]
    if selection["algorithm"] != "sha256_weighted_v1":
        raise SystemExit("unsupported city-shape selection algorithm")
    if not 0 <= selection["repetition_penalty"]:
        raise SystemExit("city-shape repetition penalty must not be negative")

    declared = tuple(catalogue["site_features"])
    if set(declared) != set(FEATURES):
        raise SystemExit("city-shapes.json site_features are not the vocabulary this port knows: %s"
                         % sorted(set(declared) ^ set(FEATURES)))
    if not set(selection["required_site_fields"]) <= set(FEATURES):
        raise SystemExit("a required site field is outside the feature vocabulary")

    seen = set()
    for shape in catalogue["shapes"]:
        if shape["id"] in seen:
            raise SystemExit("duplicate city-shape id %s" % shape["id"])
        seen.add(shape["id"])
        if not set(shape["city_classes"]) <= set(CLASSES) or not shape["city_classes"]:
            raise SystemExit("shape %s has an invalid city class list" % shape["id"])
        if not 0 < shape["base_weight"] <= 1000:
            raise SystemExit("shape %s has an out-of-range base weight" % shape["id"])
        for rule in shape["eligibility"]["all"]:
            _check_rule(catalogue, shape["id"], rule)
        for preference in shape["preferences"]:
            _check_rule(catalogue, shape["id"], preference["when"])
            if not 0 <= preference["bonus"] <= 1000:
                raise SystemExit("shape %s has an out-of-range preference bonus" % shape["id"])
        for key, interval in shape["variation"].items():
            if interval["min"] > interval["max"]:
                raise SystemExit("shape %s variation %s is reversed" % (shape["id"], key))
            if interval.get("integer") and not all(isinstance(interval[k], int) for k in ("min", "max")):
                raise SystemExit("shape %s variation %s needs integer endpoints" % (shape["id"], key))

    # The identity is what the digest is FOR. Recomputing it here and refusing a
    # mismatch means the recorded value can never drift away from the bytes beside it.
    identity = hashlib.sha256(_canonical(catalogue).encode()).hexdigest()
    if identity != doc["selection_identity_sha256"]:
        raise SystemExit("city-shapes.json selection_identity_sha256 does not match the catalogue: "
                         "recorded %s, computed %s" % (doc["selection_identity_sha256"], identity))
    return doc, catalogue, identity


def _check_rule(catalogue, shape_id, rule):
    field = rule.get("field")
    if field not in catalogue["site_features"]:
        raise SystemExit("shape %s tests unknown site feature %r" % (shape_id, field))
    if set(rule) - {"field", "min", "max", "equals"} or len(rule) < 2:
        raise SystemExit("shape %s has a malformed rule on %s" % (shape_id, field))
    boolean = catalogue["site_features"][field]["type"] == "boolean"
    if boolean and (set(rule) != {"field", "equals"} or not isinstance(rule["equals"], bool)):
        raise SystemExit("shape %s must compare boolean %s with an explicit equals" % (shape_id, field))
    if not boolean and "equals" in rule and isinstance(rule["equals"], bool):
        raise SystemExit("shape %s compares numeric %s with a boolean" % (shape_id, field))


def _rule_literal(rule):
    # A boolean feature is stored as 1.0 / 0.0, so `equals: true` and `equals: 1` are
    # the same comparison and the rule engine needs only one kind of test.
    equals = float(rule["equals"]) if "equals" in rule else 0.0
    return ("{ShapeFeature::%s, %s, %s, %s, %s, %s, %s}"
            % (rule["field"],
               cpp_double(rule.get("min", 0.0)), cpp_double(rule.get("max", 0.0)),
               cpp_double(equals),
               "true" if "min" in rule else "false",
               "true" if "max" in rule else "false",
               "true" if "equals" in rule else "false"))


def _chunks(text, width=2000):
    return [text[i:i + width] for i in range(0, len(text), width)]


def render_city_shapes():
    doc, catalogue, identity = load_city_shapes()
    # _rank walks sorted(shapes, key=id) and draws parameters in sorted(variation) key
    # order, so both sorts are baked in here and the table IS the draw order.
    shapes = sorted(catalogue["shapes"], key=lambda s: s["id"])

    rules, preferences, variations, sources, layout = [], [], [], [], []
    rows = []
    for shape in shapes:
        # The shape's OWN layout keys, in its own order. Seven shapes carry seven and
        # one carries an eighth (fortification_mode); a fixed seven-field record would
        # drop it exactly the way the reference's layout-profile tuple drops a feature.
        layout_begin = len(layout)
        for key, text in shape["layout"].items():
            layout.append("{%s, %s}" % (cpp_string(key), cpp_string(text)))
        rule_begin = len(rules)
        for rule in shape["eligibility"]["all"]:
            rules.append(_rule_literal(rule))
        pref_begin = len(preferences)
        for preference in shape["preferences"]:
            preferences.append("{%s, %s}" % (_rule_literal(preference["when"]),
                                             cpp_double(preference["bonus"])))
        var_begin = len(variations)
        for key in sorted(shape["variation"]):
            interval = shape["variation"][key]
            variations.append("{%s, %s, %s, %s}"
                              % (cpp_string(key), cpp_double(interval["min"]),
                                 cpp_double(interval["max"]),
                                 "true" if interval.get("integer") else "false"))
        source_begin = len(sources)
        sources.extend(cpp_string(s) for s in shape["history"]["source_ids"])
        classes = 0
        for bit, name in enumerate(CLASSES):
            if name in shape["city_classes"]:
                classes |= 1 << bit
        rows.append("    {%s, %s, %s, %s, %du, %s, {kRules + %d, %d}, {kPreferences + %d, %d}, "
                    "{kVariations + %d, %d}, {kSourceIds + %d, %d}, {kLayout + %d, %d}},"
                    % (cpp_string(shape["id"]), cpp_string(shape["name"]), cpp_string(shape["family"]),
                       "true" if shape["enabled_by_default"] else "false", classes,
                       cpp_double(shape["base_weight"]),
                       rule_begin, len(rules) - rule_begin,
                       pref_begin, len(preferences) - pref_begin,
                       var_begin, len(variations) - var_begin,
                       source_begin, len(sources) - source_begin,
                       layout_begin, len(layout) - layout_begin))

    lines = [BANNER.format(source="city-shapes.json")]
    lines.append('#include "fwg/city_plan.hpp"\n')
    lines.append('#include "fwg/sha256.hpp"\n')
    lines.append("#include <string>\n")
    lines.append("namespace fwg {")
    lines.append("namespace {\n")

    lines.append("constexpr ShapeRule kRules[] = {")
    for rule in rules:
        lines.append("    %s," % rule)
    lines.append("};\n")
    lines.append("constexpr ShapePreference kPreferences[] = {")
    for pref in preferences:
        lines.append("    %s," % pref)
    lines.append("};\n")
    lines.append("constexpr ShapeVariation kVariations[] = {")
    for var in variations:
        lines.append("    %s," % var)
    lines.append("};\n")
    lines.append("constexpr std::string_view kSourceIds[] = {%s};\n" % ", ".join(sources))
    lines.append("// The authored design guidance for each shape, in the shape's own key order.")
    lines.append("// Carried because the catalogue identity hashes this prose, so it already")
    lines.append("// decides which shape a city is given; a reader should be able to see it.")
    lines.append("constexpr ShapeLayoutNote kLayout[] = {")
    for note in layout:
        lines.append("    %s," % note)
    lines.append("};\n")

    # The declared type and range of every feature, in enum order. _rank refuses a site
    # value outside these, which is live: regional_threat is declared 0..1, so a threat
    # assessment writing 1.2 stops city planning rather than skewing every shape draw.
    lines.append("constexpr ShapeFeatureSpec kFeatureSpecs[] = {")
    for name in FEATURES:
        definition = catalogue["site_features"][name]
        boolean = definition["type"] == "boolean"
        lines.append("    {%s, %s, %s},  // %s"
                     % ("true" if boolean else "false",
                        cpp_double(0.0 if boolean else definition["min"]),
                        cpp_double(1.0 if boolean else definition["max"]), name))
    lines.append("};\n")

    lines.append("// ALREADY IN DRAW ORDER: sorted by id, because _rank ranks by id and the")
    lines.append("// cumulative weighted walk therefore subtracts in id order, not list order.")
    lines.append("constexpr CityShape kShapes[] = {")
    lines.extend(rows)
    lines.append("};\n")

    canonical = _canonical(catalogue)
    lines.append("// The exact bytes the identity digest is taken over. Split only because MSVC")
    lines.append("// refuses a string literal past 65535 bytes; the concatenation is the document.")
    lines.append("constexpr std::string_view kCanonical[] = {")
    for chunk in _chunks(canonical):
        lines.append("    %s," % cpp_string(chunk))
    lines.append("};\n")
    lines.append("}  // namespace\n")

    lines.append("std::span<const CityShape> city_shapes() { return kShapes; }\n")
    lines.append("std::span<const ShapeFeatureSpec> shape_feature_specs() { return kFeatureSpecs; }\n")
    lines.append("int city_shape_catalogue_revision() { return %d; }\n" % catalogue["revision"])
    lines.append("double shape_repetition_penalty() { return %s; }\n"
                 % cpp_double(catalogue["selection"]["repetition_penalty"]))

    lines.append("// Hashed here rather than recorded, so the digest cannot outlive the bytes it")
    lines.append("// describes. data/city-shapes.json records the expected value and the codegen")
    lines.append("// refuses a mismatch; this reproduces it from the embedded document.")
    lines.append("const std::string& city_shape_catalogue_identity() {")
    lines.append("    static const std::string identity = [] {")
    lines.append("        std::string document;")
    lines.append("        for (std::string_view chunk : kCanonical) document.append(chunk);")
    lines.append("        return sha256_hex(document);")
    lines.append("    }();")
    lines.append("    return identity;")
    lines.append("}\n")

    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {
    OUT / "content_city_shapes.cpp": render_city_shapes,
}
