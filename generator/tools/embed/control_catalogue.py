"""world-options.json + config-controls.json + control-annotations.json
-> src/generated/content_controls.cpp.

The published control catalogue: every knob a request may set, what it means and what it
accepts, plus three things a caller cannot see from bounds alone and each of which changes
what a request does. Derived exactly as the reference's tools/export_controls.py derived
them, from the same two halves of the control surface:

  * `status`: `pinned` when min == max (every override is refused), `inert` when
    control-annotations.json names it (accepted and read by nothing), `open` otherwise.
  * `derived_from` / `note` on each control the world's physical width derives, and
    `overrides` on the two width inputs that derive them.
  * `producers`: who honours the control. See control-annotations.json for why `native`
    now means this producer.

Rows keep the reference's field set and are emitted with sorted keys at every level, as the
reference's catalogue file is written, so `describe_controls` answers with the same key
order the reference's does.

Fail-closed on: the two halves overlapping, a control with no description (the reference's
registry refuses one at import), an annotation naming a control that does not exist, and
population_profile's vocabulary drifting from civilizations.json, which is its source.
"""
from embed.common import BANNER, OUT, load
from embed.json_text import json_text, text_function

ANNOTATIONS = "control-annotations.json"
ROW_KEYS = ("default", "min", "max", "type", "units", "group", "description")


def load_catalogue():
    options = load("world-options.json", 1)["options"]
    config = load("config-controls.json", 1)["controls"]
    notes = load(ANNOTATIONS, 1)
    if notes.get("schema") != "fwg.control-annotations":
        raise SystemExit("%s must declare schema 'fwg.control-annotations'" % ANNOTATIONS)

    overlap = sorted(set(options) & set(config))
    if overlap:
        raise SystemExit("world-options.json and config-controls.json both define %s" % overlap)
    controls = dict(options)
    controls.update(config)

    for name, definition in controls.items():
        if not definition.get("description"):
            raise SystemExit("control %s is published with no description" % name)
        if definition.get("type") not in ("number", "integer", "string"):
            raise SystemExit("control %s has type %r" % (name, definition.get("type")))
        if definition["type"] != "string" and not definition["min"] <= definition["default"] <= definition["max"]:
            raise SystemExit("control %s default lies outside its own bounds" % name)

    shape_inputs, derived, inert = notes["shape_inputs"], notes["derived"], notes["inert"]
    for name in list(shape_inputs) + list(derived) + list(inert):
        if name not in controls:
            raise SystemExit("%s names %r, which is not a control" % (ANNOTATIONS, name))
    if len(set(derived)) != len(derived) or set(derived) & set(shape_inputs):
        raise SystemExit("%s: derived controls must be distinct and not width inputs" % ANNOTATIONS)

    profiles = load("civilizations.json", 1)["profiles"]
    entities = [p["id"] for p in profiles if p["kind"] == "entity"]
    population = controls["population_profile"]
    if population["choices"] != ["mixed"] + entities:
        raise SystemExit("population_profile.choices %s is not ['mixed'] + the civilization entities %s; "
                         "regenerate it from civilizations.json" % (population["choices"], entities))
    names = {p["id"]: p["name"] for p in profiles}
    if population["choice_labels"] != {k: names.get(k) for k in population["choice_labels"]} \
            or set(population["choice_labels"]) != set(population["choices"]):
        raise SystemExit("population_profile.choice_labels disagrees with civilizations.json names")

    rows = {}
    for name in sorted(controls):
        definition = controls[name]
        row = {key: definition[key] for key in ROW_KEYS if key in definition}
        for optional in ("choices", "choice_labels"):
            if optional in definition:
                row[optional] = definition[optional]
        low, high = definition.get("min"), definition.get("max")
        if low is not None and low == high:
            row["status"] = "pinned"
            row["pinned_reason"] = "min == max == %s, so every override of this control is rejected." % low
        elif name in inert:
            row["status"] = "inert"
            row["inert_reason"] = inert[name]
        else:
            row["status"] = "open"
        if name in derived:
            row["derived_from"] = list(shape_inputs)
            row["note"] = notes["derived_note"]
        if name in shape_inputs:
            row["overrides"] = list(derived)
        row["producers"] = list(notes["producers"])
        rows[name] = row

    catalogue = notes["catalogue"]
    return {"schema": catalogue["schema"], "version": catalogue["version"],
            "recipe_version": catalogue["recipe_version"], "controls": rows}


def render_controls():
    body = load_catalogue()
    lines = [BANNER.format(source="world-options.json, config-controls.json and " + ANNOTATIONS)]
    lines.append('#include "fwg/product_content.hpp"\n')
    lines.append("#include <string>\n")
    lines.append("namespace fwg {\n")
    lines.extend(text_function("world_controls_json", json_text(body, sort_keys=True),
                               "the control catalogue, %d controls" % len(body["controls"])))
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_control_catalogue.cpp": render_controls}
