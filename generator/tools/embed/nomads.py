"""The nomad balance table: six classifications, a candidate rate and a fission rule.

THE FILE'S KEY ORDER IS FREE AND THAT IS DELIBERATE. The reference builds its per-cell
share list by iterating a Python tuple, not the document, and its loader checks the key
SET only -- so re-sorting `classifications` in the JSON is a no-op while reordering that
tuple renumbers anything that stored an index into it. CLASSIFICATIONS below is this
port's copy of that tuple; the table is emitted in ITS order, never the file's, and the
file's own `classification_order` is read for one purpose only: to fail the build if the
two ever disagree.

NO IDENTITY IS STAMPED. Unlike castles.json and pantheon.json, nothing hashes this
document and nothing compares it on reopen, so editing a number silently changes new
worlds while saved worlds keep their old bands. That is inherited from the reference and
recorded rather than fixed -- a hash here would be a new contract, not a port.

Validation mirrors the reference loader's and is fail-closed for the same reason a
catalogue's is: a table that has lost a key is not a looser table, it is a broken one.
The reference reads a gate key with a fallback at every call site, so a lost key silently
becomes a default and the world changes without a word. Here the keys a GATE reads are
required and the build fails without them; only the keys a ROUTE BUILDER reads with its
own documented fallback are allowed to be absent, and those fall back exactly as before.
"""
from embed.common import BANNER, OUT, cpp_double, cpp_string, load

SOURCE = "nomads.json"

# EVALUATION ORDER, MOST SPECIFIC FIRST, and the same tuple the reference walks. It must
# match include/fwg/nomads.hpp's NomadClass enum position for position, because the
# generated table is indexed by that enum. APPEND, NEVER INSERT.
CLASSIFICATIONS = ("survivors", "deserters", "cultists", "merchants", "bandits", "wanderers")

ENUM = {"survivors": "Survivors", "deserters": "Deserters", "cultists": "Cultists",
        "merchants": "Merchants", "bandits": "Bandits", "wanderers": "Wanderers"}

DISPOSITIONS = {"hostile": "Hostile", "wary": "Wary", "tradeable": "Tradeable",
                "devout": "Devout", "fleeing": "Fleeing"}

# NomadGate's fields, in DECLARATION ORDER -- C++20 designated initialisers must appear in
# it -- with the C++ type of each. Emitting only the keys a classification actually
# authors keeps the generated file a readable statement of what was authored, and leaves
# every other key at the header's documented fallback.
GATE_FIELDS = (
    ("ruin_reach_spacings", "double"),
    ("refuge_reach_spacings", "double"),
    ("war_reach_spacings", "double"),
    ("min_ruggedness", "double"),
    ("min_ley_intensity", "double"),
    ("ley_reach_spacings", "double"),
    ("shrine_reach_spacings", "double"),
    ("claim_reach_spacings", "double"),
    ("road_reach_spacings", "double"),
    ("market_reach_spacings", "double"),
    ("min_markets", "int"),
    ("min_market_population", "double"),
    ("strike_reach_spacings", "double"),
    ("control_clear_spacings", "double"),
    ("min_forage", "double"),
    ("min_seasonal_swing", "double"),
    ("summer_reach_spacings", "double"),
    ("winter_reach_spacings", "double"),
)
GATE_ORDER = {name: i for i, (name, _) in enumerate(GATE_FIELDS)}

# The keys each GATE reads without a fallback. A file missing one of these fails the build
# rather than generating a table whose gate answers a different question.
REQUIRED_GATE_KEYS = {
    "survivors": ("ruin_reach_spacings", "refuge_reach_spacings"),
    "deserters": ("war_reach_spacings", "min_ruggedness"),
    "cultists": ("min_ley_intensity", "ley_reach_spacings", "shrine_reach_spacings",
                 "claim_reach_spacings"),
    "merchants": ("road_reach_spacings", "market_reach_spacings", "min_markets",
                  "min_market_population"),
    "bandits": ("min_ruggedness", "strike_reach_spacings", "control_clear_spacings"),
    "wanderers": ("min_forage", "min_seasonal_swing"),
}


def load_nomad_policy():
    doc = load(SOURCE, 1)
    pol = doc.get("policy")
    if not isinstance(pol, dict):
        raise SystemExit("nomads.json must carry a policy document")
    if pol.get("version") != 2:
        raise SystemExit("unsupported nomad policy version: %r" % pol.get("version"))

    # The file records the tuple order so the two can be compared. It is a CHECK, never a
    # source: taking the order from here would put a contract the code owns into data.
    recorded = doc.get("classification_order")
    if list(recorded or ()) != list(CLASSIFICATIONS):
        raise SystemExit("nomads.json classification_order %r does not match the port's "
                         "CLASSIFICATIONS %r -- one of the two moved, and the order is a "
                         "contract" % (recorded, list(CLASSIFICATIONS)))

    classes = pol.get("classifications")
    if not isinstance(classes, dict) or set(classes) != set(CLASSIFICATIONS):
        raise SystemExit("nomads.json must define exactly the known classifications")

    for name in CLASSIFICATIONS:
        entry = classes[name]
        if not float(entry.get("weight", 0)) > 0:
            raise SystemExit("nomad weight must be positive: " + name)
        low, high = entry["size"]
        if not 0 < low <= high:
            raise SystemExit("nomad size band must be positive and ordered: " + name)
        if not float(entry.get("speed_m_per_day", 0)) > 0:
            raise SystemExit("nomad speed must be positive: " + name)
        gate = entry.get("gate")
        if not isinstance(gate, dict):
            raise SystemExit("nomad classification needs a gate: " + name)
        unknown = sorted(set(gate) - set(GATE_ORDER))
        if unknown:
            raise SystemExit("nomad gate %s names key(s) no gate reads: %s -- a typo here "
                             "would silently fall back to a default" % (name, unknown))
        missing = [k for k in REQUIRED_GATE_KEYS[name] if k not in gate]
        if missing:
            raise SystemExit("nomad gate %s is missing key(s) its own gate reads: %s"
                             % (name, missing))
        if not entry.get("seeks") or not entry.get("carries"):
            raise SystemExit("every band writes seeks and carries: " + name)
        if entry["disposition"] not in DISPOSITIONS:
            raise SystemExit("unknown nomad disposition %r on %s" % (entry["disposition"], name))

    candidate = pol.get("candidate")
    if not isinstance(candidate, dict):
        raise SystemExit("nomads.json must define a candidate section")

    split = pol.get("fission")
    if not isinstance(split, dict):
        raise SystemExit("nomads.json must define a fission section")
    kinds = list(split.get("classifications") or ())
    if not set(kinds) <= set(CLASSIFICATIONS):
        raise SystemExit("fission names a classification that does not exist")
    share_low, share_high = split["child_share"]
    if not 0 < share_low <= share_high < 1:
        raise SystemExit("fission child share must be an ordered fraction below one")
    for name in kinds:
        # The capacity curve runs from the gate's own forage floor to this number, so a
        # rich_forage at or below the floor divides by zero width and makes every band of
        # that class either always or never over capacity.
        if not float(split.get("rich_forage", 0)) > float(classes[name]["gate"].get("min_forage", 0.)):
            raise SystemExit("fission rich_forage must exceed the gate forage floor: " + name)
    return pol


def gate_literal(gate):
    """A NomadGate as C++20 designated initialisers, in declaration order.

    Only the authored keys appear. Every other key keeps the header's fallback, which is
    the value the reference's own `gate.get(key, fallback)` would have produced -- so the
    two halves of the reference's dict lookup survive as a typed struct.
    """
    parts = []
    for key in sorted(gate, key=lambda k: GATE_ORDER[k]):
        kind = GATE_FIELDS[GATE_ORDER[key]][1]
        value = "%d" % int(gate[key]) if kind == "int" else cpp_double(gate[key])
        parts.append(".%s = %s" % (key, value))
    return "{%s}" % ", ".join(parts)


def render_nomads():
    pol = load_nomad_policy()
    classes = pol["classifications"]

    seeks, carries, spans = [], [], {}
    for name in CLASSIFICATIONS:
        entry = classes[name]
        s_begin, c_begin = len(seeks), len(carries)
        seeks.extend(entry["seeks"])
        carries.extend(entry["carries"])
        spans[name] = (s_begin, len(seeks) - s_begin, c_begin, len(carries) - c_begin)

    lines = [BANNER.format(source=SOURCE)]
    lines.append('#include "fwg/nomads.hpp"\n')
    lines.append("namespace fwg {")
    lines.append("namespace {\n")

    lines.append("// What each kind of band is walking toward and what it has with it. Nothing in a")
    lines.append("// nomad pass reads either; every band record publishes both, and a fission child")
    lines.append("// copies the arrays of the classification it inherits.")
    lines.append("constexpr std::string_view kSeeks[] = {")
    for value in seeks:
        lines.append("    %s," % cpp_string(value))
    lines.append("};\n")
    lines.append("constexpr std::string_view kCarries[] = {")
    for value in carries:
        lines.append("    %s," % cpp_string(value))
    lines.append("};\n")

    kinds = [n for n in CLASSIFICATIONS if n in set(pol["fission"]["classifications"])]
    lines.append("// Only herders split today. Ordered by the enum rather than by the file, for the")
    lines.append("// same reason the table below is.")
    lines.append("constexpr NomadClass kFissionClasses[] = {")
    for name in kinds:
        lines.append("    NomadClass::%s," % ENUM[name])
    lines.append("};\n")

    lines.append("// IN NomadClass ORDER, NOT FILE ORDER. The gates are evaluated in this order, the")
    lines.append("// share list is built in it, `total` accumulates in it and the cumulative pick")
    lines.append("// walks it -- so reordering the same rows is a different world on the same seed.")
    lines.append("// The table is indexed by the enum, which is what makes that structural.")
    lines.append("constexpr NomadClassPolicy kClasses[] = {")
    for name in CLASSIFICATIONS:
        entry = classes[name]
        s_begin, s_count, c_begin, c_count = spans[name]
        low, high = entry["size"]
        lines.append("    {%s, %s, NomadDisposition::%s, %s, %d, %d, %s,"
                     % (cpp_string(name), cpp_double(entry["weight"]),
                        DISPOSITIONS[entry["disposition"]], cpp_double(entry["speed_m_per_day"]),
                        int(low), int(high), cpp_double(entry["column_length_per_head_m"])))
        lines.append("     {kSeeks + %d, %d}, {kCarries + %d, %d},"
                     % (s_begin, s_count, c_begin, c_count))
        lines.append("     %s}," % gate_literal(entry["gate"]))
    lines.append("};\n")

    candidate = pol["candidate"]
    lines.append("// `min_suitability` is carried for fidelity of the data file and is NEVER READ: no")
    lines.append("// nomad code in the reference consults it, and inventing a suitability floor would")
    lines.append("// change every world's band count against no reference behaviour at all.")
    lines.append("constexpr NomadPolicy kPolicy = {")
    lines.append("    %d," % pol["version"])
    lines.append("    {%s, %s, %s}," % (cpp_double(candidate["density_per_1000km2"]),
                                        cpp_double(candidate["settlement_clearance_spacings"]),
                                        cpp_double(candidate.get("min_suitability", 0.))))
    lines.append("    {{kFissionClasses, %d}, %s, %s, %s},"
                 % (len(kinds), cpp_double(pol["fission"]["rich_forage"]),
                    cpp_double(pol["fission"]["child_share"][0]),
                    cpp_double(pol["fission"]["child_share"][1])))
    lines.append("    {kClasses, %d},"  % len(CLASSIFICATIONS))
    lines.append("};\n")

    lines.append("}  // namespace\n")
    lines.append("const NomadPolicy& nomad_policy() { return kPolicy; }\n")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_nomads.cpp": render_nomads}
