"""The whole published control surface: every knob a request may set, with its bounds, its
units and the sentence that says what it does.

Two documents make it up and they are disjoint. config-controls.json holds the 45 controls
that are Config fields (plus the two authored inputs, circumference_km and relief_m, that the
request builder consumes); world-options.json holds the 164 world options. Their union is the
reference's terrain_world.registry(3), and the world document publishes it as
`recipe.parameters` so a consumer holding a world can rebuild the request form without a
second call.

world_options.cpp already embeds the options' NUMBERS, because generation reads them. This
table adds what generation never reads -- the prose, the choices, the Config half -- so a
wrong sentence here changes a document and never a world. It is emitted sorted by name,
which is the order both source documents already use; nothing reads it by position.

DERIVED, NOT AUTHORED HERE: population_profile's `choices` and `choice_labels`. The note in
config-controls.json says they are copied out of the civilization registry, so they are
checked against civilizations.json rather than trusted: a registry that gains a people and a
control surface that does not offer it is a request nobody can make.
"""
from embed.common import BANNER, OUT, cpp_double, cpp_string, load

CONFIG_SOURCE = "config-controls.json"
OPTIONS_SOURCE = "world-options.json"
TYPES = {"number": "Number", "integer": "Integer", "string": "String"}


def civilization_choices():
    """(choices, labels) exactly as the reference registry derives them."""
    doc = load("civilizations.json", 1)
    entities = [p["id"] for p in doc["profiles"] if p["kind"] == "entity"]
    labels = [(p["id"], p["name"]) for p in doc["profiles"]]
    return ["mixed"] + entities, labels


def check_control(name, spec, source):
    for key in ("default", "description", "group", "type", "units"):
        if key not in spec:
            raise SystemExit("%s: control %s has no %s" % (source, name, key))
    if spec["type"] not in TYPES:
        raise SystemExit("%s: control %s has an unknown type %r" % (source, name, spec["type"]))
    if not isinstance(spec["description"], str) or not spec["description"].strip():
        # registry() raises at import on an undescribed control; so does this.
        raise SystemExit("%s: control %s is published with no description" % (source, name))
    if spec["type"] == "string":
        if "min" in spec or "max" in spec:
            raise SystemExit("%s: string control %s carries numeric bounds" % (source, name))
        if not isinstance(spec["default"], str):
            raise SystemExit("%s: string control %s has a non-string default" % (source, name))
        choices = spec.get("choices")
        if choices is not None and spec["default"] not in choices:
            raise SystemExit("%s: control %s defaults outside its own choices" % (source, name))
    else:
        if isinstance(spec["default"], bool) or not isinstance(spec["default"], (int, float)):
            raise SystemExit("%s: numeric control %s has a non-numeric default" % (source, name))
        if "choices" in spec or "choice_labels" in spec:
            raise SystemExit("%s: numeric control %s carries choices" % (source, name))
        if ("min" in spec) != ("max" in spec):
            raise SystemExit("%s: control %s has one bound and not the other" % (source, name))
        if "min" in spec and not spec["min"] <= spec["default"] <= spec["max"]:
            raise SystemExit("%s: control %s defaults outside its own bounds" % (source, name))
        if spec["type"] == "integer" and float(spec["default"]) != int(spec["default"]):
            raise SystemExit("%s: integer control %s has a fractional default" % (source, name))


def load_controls():
    config = load(CONFIG_SOURCE, 1)
    options = load(OPTIONS_SOURCE, 1)
    if config.get("recipe_version") != 3:
        raise SystemExit("%s must describe recipe 3, the only live recipe" % CONFIG_SOURCE)
    config_rows = config["controls"]
    option_rows = options["options"]
    overlap = sorted(set(config_rows) & set(option_rows))
    if overlap:
        raise SystemExit("controls %s are both a Config field and a world option" % overlap)

    choices, labels = civilization_choices()
    profile = config_rows.get("population_profile")
    if profile is None:
        raise SystemExit("%s has no population_profile control" % CONFIG_SOURCE)
    if profile.get("choices") != choices:
        raise SystemExit("population_profile choices %r disagree with the civilization registry %r; "
                         "regenerate them from civilizations.json" % (profile.get("choices"), choices))
    if list(profile.get("choice_labels", {}).items()) != labels:
        raise SystemExit("population_profile choice_labels disagree with the civilization registry")

    rows = []
    for name, spec in config_rows.items():
        check_control(name, spec, CONFIG_SOURCE)
        rows.append((name, spec, False))
    for name, spec in option_rows.items():
        check_control(name, spec, OPTIONS_SOURCE)
        if spec["type"] == "string":
            raise SystemExit("%s: world option %s is a string; world options are numbers" % (OPTIONS_SOURCE, name))
        rows.append((name, spec, True))
    rows.sort(key=lambda row: row[0])
    return rows


def render_controls():
    rows = load_controls()
    lines = [BANNER.format(source=CONFIG_SOURCE + " and data/" + OPTIONS_SOURCE)]
    lines.append('#include "fwg/controls.hpp"\n')
    lines.append("#include <algorithm>\n")
    lines.append("namespace fwg {\nnamespace {\n")

    for name, spec, _ in rows:
        if spec.get("choices"):
            lines.append("constexpr std::string_view kChoices_%s[] = {%s};"
                         % (name, ", ".join(cpp_string(c) for c in spec["choices"])))
        if spec.get("choice_labels"):
            lines.append("constexpr ControlChoiceLabel kLabels_%s[] = {%s};"
                         % (name, ", ".join("{%s, %s}" % (cpp_string(k), cpp_string(v))
                                            for k, v in spec["choice_labels"].items())))
    lines.append("")

    lines.append("// Sorted by name so control_spec can binary-search it. Nothing reads it by position.")
    lines.append("constexpr ControlSpec kControls[] = {")
    for name, spec, is_option in rows:
        numeric = spec["type"] != "string"
        lines.append("    {%s, ControlType::%s, %s, %s, %s, %s, %s, %s,"
                     % (cpp_string(name), TYPES[spec["type"]],
                        cpp_double(spec["default"]) if numeric else "0.0",
                        cpp_string(spec["default"]) if not numeric else '""',
                        "true" if "min" in spec else "false",
                        cpp_double(spec["min"]) if "min" in spec else "0.0",
                        "true" if "max" in spec else "false",
                        cpp_double(spec["max"]) if "max" in spec else "0.0"))
        lines.append("     %s, %s, %s,"
                     % (cpp_string(spec["group"]), cpp_string(spec["description"]), cpp_string(spec["units"])))
        lines.append("     %s, %s, %s},"
                     % ("kChoices_%s" % name if spec.get("choices") else "{}",
                        "kLabels_%s" % name if spec.get("choice_labels") else "{}",
                        "true" if is_option else "false"))
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::span<const ControlSpec> control_specs() { return kControls; }\n")
    lines.append("""const ControlSpec* control_spec(std::string_view name) {
    const auto* first = std::begin(kControls);
    const auto* last = std::end(kControls);
    const auto* it = std::lower_bound(first, last, name,
                                      [](const ControlSpec& spec, std::string_view key) { return spec.name < key; });
    if (it == last || it->name != name) return nullptr;
    return it;
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_controls.cpp": render_controls}
