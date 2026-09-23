"""The sentence a city leaves behind when a leyline killed it, one per KNOWN school.

Prose and nothing else. The fate lottery weighs a ley cause by its pressure and never reads
this string, so revising a sentence changes the text of a ruin and no world.

THE FILE'S KEY ORDER IS FREE: the lookup is by school name. The table is emitted in
school-table order, read from magic-schools.json, only so the generated file lists the
schools in the order every other table does.

Fail-closed, and the key set must be EXACTLY the known schools. A missing school is a ruin
with no reason. A hidden school is a dead row -- generation can never raise one -- and worse
than dead: the C++ refuses a hidden-school ley fate loudly, and a row here would quietly turn
that refusal into invented prose on every world the corruption API has touched.
"""
from embed.common import BANNER, OUT, cpp_string, load

SOURCE = "ley-destruction.json"
SCHEMA = "fwg.ley-destruction"


def known_schools():
    """The known schools in contract order, from the schools document itself."""
    doc = load("magic-schools.json", 1)
    return [row["name"] for row in doc["schools"] if not row["hidden"]]


def load_reasons():
    doc = load(SOURCE, 1)
    if doc.get("schema") != SCHEMA:
        raise SystemExit("%s must declare schema %r" % (SOURCE, SCHEMA))
    reasons = doc.get("reasons")
    if not isinstance(reasons, dict):
        raise SystemExit("%s must carry a reasons object" % SOURCE)
    known = known_schools()
    missing = sorted(set(known) - set(reasons))
    extra = sorted(set(reasons) - set(known))
    if missing:
        raise SystemExit("%s has no fall reason for known school(s) %s" % (SOURCE, missing))
    if extra:
        raise SystemExit("%s keys %s, which are not known schools -- a hidden school is never "
                         "raised by generation and must not be given a reason" % (SOURCE, extra))
    for school in known:
        text = reasons[school]
        if not isinstance(text, str) or not text.strip():
            raise SystemExit("%s: the %s reason must be a non-empty string" % (SOURCE, school))
    return [(school, reasons[school]) for school in known]


def render_ley_destruction():
    rows = load_reasons()
    lines = [BANNER.format(source=SOURCE)]
    lines.append('#include "fwg/fates.hpp"\n')
    lines.append("namespace fwg {")
    lines.append("namespace {\n")
    lines.append("struct LeyDestructionRow {")
    lines.append("    std::string_view school;")
    lines.append("    std::string_view reason;")
    lines.append("};\n")
    lines.append("// The eight known schools, in school-table order. No hidden school has a row, and")
    lines.append("// that is the contract rather than a gap: see tools/embed/ley_destruction.py.")
    lines.append("constexpr LeyDestructionRow kRows[] = {")
    for school, text in rows:
        lines.append("    {%s, %s}," % (cpp_string(school), cpp_string(text)))
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::string_view ley_destruction_reason(std::string_view school) {")
    lines.append("    for (const LeyDestructionRow& row : kRows) {")
    lines.append("        if (row.school == school) return row.reason;")
    lines.append("    }")
    lines.append("    return {};")
    lines.append("}\n")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_ley_destruction.cpp": render_ley_destruction}
