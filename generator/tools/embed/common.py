"""Helpers shared by every dataset renderer.

These live apart from embed_content.py so a dataset module can use them without
importing the driver that imports it.
"""
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
DATA = ROOT / "data"
OUT = ROOT / "src" / "generated"


def load(name, schema_version=1):
    """Read a data/*.json document and check the schema version it declares."""
    doc = json.loads((DATA / name).read_text(encoding="utf-8"))
    if doc.get("schema_version") != schema_version:
        raise SystemExit("%s must declare schema version %d" % (name, schema_version))
    return doc


def sha256_of(text):
    """The digest a generated table records, over the bytes it was built from."""
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


BANNER = """// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/{source}.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.
"""



def cpp_string(text):
    out = text.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + out.replace("\n", "\\n").replace("\t", "\\t") + '"'



def cpp_double(v):
    # Round-trip exactly, and keep it a double literal rather than an int.
    text = repr(float(v))
    return text if ("." in text or "e" in text or "E" in text or "inf" in text or "nan" in text) else text + ".0"



HABITAT_FIELDS = ("biome", "variant", "temperature", "moisture", "maritime", "landmass_area_m2",
                  "landmass_fraction", "largest_landmass", "resource", "slope", "height", "tpi",
                  "coastal_support", "abs_latitude")



def habitat_node_literal(n):
    """One flattened habitat node as a C++ aggregate initialiser.

    Shared by the civilization profiles and the entity settlement rules, which are
    separate trees over the same node type.
    """
    return ("{HabitatOp::%s, HabitatField::%s, %s, %s, %s, %s, %s, %s, %s, %s, "
            "%du, %du, %du, %du, %du, %du}"
            % (n["op"], n["field"],
               cpp_double(n.get("min") or 0.0), cpp_double(n.get("max") or 0.0),
               cpp_double(n.get("gt") or 0.0), cpp_double(n.get("equals") or 0.0),
               "true" if n.get("min") is not None else "false",
               "true" if n.get("max") is not None else "false",
               "true" if n.get("gt") is not None else "false",
               "true" if n.get("equals") is not None else "false",
               n["num"][0], n["num"][1], n["txt"][0], n["txt"][1],
               n["child"][0], n["child"][1]))


def optional_double(value):
    """A std::optional<double> literal.

    `null` is NOT zero on the fields that use this. A null freshwater multiplier means
    the entity is not gated on fresh water at all, where 0 would forbid every cell; a
    null resource minimum means that terminal is NEVER built, where 0 would build one
    everywhere.
    """
    return "std::nullopt" if value is None else "%s" % cpp_double(value)


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


