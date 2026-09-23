"""What the world document's `civilizations` block republishes and no generation step reads.

The reference's terrain_civilizations.civilization_report copies three things out of the
registry verbatim: the `capital_scope` prose from city_classification, every key of
`defaults` that ends in `_color_rgb` (as `presentation_defaults`), and each entity's whole
`presentation` record -- including `outpost_colors`, the one presentation field
content_entities.cpp does not carry because nothing that builds a world reads it.

They are embedded here, from the same two documents the other registry tables come from,
rather than by widening those tables: this is display data, and keeping it in its own file
means a colour change regenerates one table and touches no generation input.

Fail-closed. The entity list must be the registry's own order -- `region_index` is a position
in it -- and every entity must say whether its outposts take the override colours.
"""
from embed.common import BANNER, OUT, cpp_string, load


def load_report_content():
    sections = load("registry-sections.json", 1)
    entities = load("entity-rules.json", 1)["entities"]
    registry = load("civilizations.json", 1)

    scope = sections["city_classification"].get("capital_scope")
    if not isinstance(scope, str) or not scope.strip():
        raise SystemExit("registry-sections.json city_classification.capital_scope must be prose")

    defaults = []
    for key, value in sections["defaults"].items():
        if not key.endswith("_color_rgb"):
            continue
        if (not isinstance(value, list) or len(value) != 3
                or any(isinstance(c, bool) or not isinstance(c, int) or not 0 <= c <= 255 for c in value)):
            raise SystemExit("registry-sections.json defaults.%s must be an RGB triple of 0..255" % key)
        defaults.append((key, value))
    if not defaults:
        raise SystemExit("registry-sections.json has no *_color_rgb default; the report would publish none")

    order = [p["id"] for p in registry["profiles"] if p["kind"] == "entity"]
    ids = [e["id"] for e in entities]
    if ids != order:
        raise SystemExit("entity-rules.json order %s is not the registry order %s" % (ids, order))
    outposts = []
    for e in entities:
        flag = e["presentation"].get("outpost_colors")
        if not isinstance(flag, bool):
            raise SystemExit("entity %s presentation.outpost_colors must be a boolean" % e["id"])
        outposts.append((e["id"], flag))
    return scope, defaults, outposts


def render_civilization_report():
    scope, defaults, outposts = load_report_content()
    lines = [BANNER.format(source="registry-sections.json and data/entity-rules.json")]
    lines.append('#include "fwg/civilization_report.hpp"\n')
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// The registry's `defaults` keys ending in _color_rgb, in authored order.")
    lines.append("constexpr PresentationDefault kPresentationDefaults[] = {")
    for key, rgb in defaults:
        lines.append("    {%s, {%d, %d, %d}}," % (cpp_string(key), rgb[0], rgb[1], rgb[2]))
    lines.append("};\n")
    lines.append("struct OutpostColors {")
    lines.append("    std::string_view entity_id;")
    lines.append("    bool outpost_colors;")
    lines.append("};\n")
    lines.append("// In registry order, one row per entity.")
    lines.append("constexpr OutpostColors kOutpostColors[] = {")
    for entity_id, flag in outposts:
        lines.append("    {%s, %s}," % (cpp_string(entity_id), "true" if flag else "false"))
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::string_view capital_scope() { return %s; }\n" % cpp_string(scope))
    lines.append("std::span<const PresentationDefault> presentation_defaults() { return kPresentationDefaults; }\n")
    lines.append("""std::optional<bool> entity_outpost_colors(std::string_view entity_id) {
    for (const OutpostColors& row : kOutpostColors) {
        if (row.entity_id == entity_id) return row.outpost_colors;
    }
    return std::nullopt;
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_civilization_report.cpp": render_civilization_report}
