"""The god catalogue: 25 known gods, 4 hidden ones, 11 cosmologies, 12 affinity tables.

TWO SOURCE FILES ARE MERGED INTO data/pantheon.json AND THE SPLIT IS LOAD-BEARING.
The reference hashes `pantheon.json` alone and stamps that digest into every world it
generates; three downstream gates refuse a saved world whose digest no longer matches.
The four Outside gods were given a file of their own precisely so that touching one does
not retire every saved world. This renderer therefore hashes ONLY the known half --
`KNOWN_KEYS` below -- and gives the hidden half its own separate digest.

Validation is fail-closed and mirrors the reference's `lint_catalogue()`, plus the
cross-file checks the reference could not make because its catalogue did not sit beside
the registries: races against registry-sections.json, civilizations against
civilizations.json, nest families against nest-profiles.json, schools against
magic-schools.json.
"""
import hashlib
import json

from embed.common import BANNER, OUT, cpp_double, cpp_string, load

# Every fact `world_facts` publishes, and therefore every name a cosmology's
# `weight.fact` may legally use. The reference reads it with `.get(name, 0.)`, so a typo
# silently weighs the cosmology at its base and nobody ever sees the mistake.
FACT_NAMES = {"schools", "primordial_count", "races", "civilizations_with_cities",
              "ocean_share", "sky", "families", "dragons", "ruins", "diaspora", "wars",
              "magic_enabled", "lunar_influence"}

# The numeric facts a weight can actually scale on. A weight per `schools` (a dict) or
# per `magic_enabled` (a bool) is a catalogue mistake, not a feature.
WEIGHT_FACTS = {"primordial_count", "races", "ocean_share", "sky", "dragons", "ruins",
                "diaspora", "wars", "lunar_influence", "civilizations_with_cities", "families"}

# kind -> (C++ enumerator, the payload key it must carry)
RULE_KINDS = {
    "always": ("always", None),
    "magic_enabled": ("magic_enabled", None),
    "magic_disabled": ("magic_disabled", None),
    "school": ("school", "school"),
    "race": ("race", "race"),
    "civilization_city": ("civilization_city", "ids"),
    "ocean_share": ("ocean_share", "min"),
    "sky": ("sky", None),
    "dragon": ("dragon", None),
    "nest_family": ("nest_family", "families"),
    "ruins": ("ruins", "min"),
    "diaspora": ("diaspora", None),
    "any": ("any_of", "rules"),
    "all": ("all_of", "rules"),
    "primordial_count": ("primordial_count", "min"),
    "gods": ("gods", "ids"),
    "races": ("races", "min"),
}

GOD_FAMILIES = {"school", "ancestor", "feature", "civic"}

# The keys of data/pantheon.json that make up the known catalogue. The digest below is
# taken over exactly these, so editing `hidden_gods` or `hidden_notes` -- or the
# provenance block, or the note -- cannot move it.
KNOWN_KEYS = ("schema", "schema_version", "aspect_midpoint", "nest_aspect_push",
              "affinity_weights", "fold_free_cosmologies", "fear_gods",
              "wild_push_families", "gods", "cosmologies", "affinities")
HIDDEN_KEYS = ("schema", "schema_version", "hidden_notes", "hidden_gods")

# The reference's own canonicalisation, kept so the two digests are comparable with the
# ones `source_identity` records: sorted keys (so re-indenting the file keeps its
# identity), no separators, and real UTF-8 rather than \u escapes.
def _canonical(doc):
    return json.dumps(doc, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode("utf-8")


class Pool:
    """A shared array of string_view, handed out as (begin, length) slices."""

    def __init__(self):
        self.items = []

    def add(self, values):
        begin = len(self.items)
        self.items.extend(values)
        return begin, len(self.items) - begin

    def literal(self, name):
        body = ", ".join(cpp_string(v) for v in self.items) if self.items else '""'
        return "constexpr std::string_view %s[] = {%s};" % (name, body)


def flatten_rule(rule, nodes, texts, children, base, where):
    """Append `rule` and its subtree to `nodes`; return this node's index.

    Children are recorded as INDICES in a shared pool rather than as a range of nodes,
    for the same reason the habitat trees are: a range is only correct while no child has
    children of its own, and `god_shape_beneath` is already two levels deep.
    """
    index = len(nodes)
    nodes.append(None)  # reserve, so children get higher indices

    kind = rule.get("kind")
    if kind not in RULE_KINDS:
        raise SystemExit("%s uses unknown existence rule %r" % (where, kind))
    enumerator, payload = RULE_KINDS[kind]
    if payload is not None and payload not in rule:
        raise SystemExit("%s rule %r needs a %r" % (where, kind, payload))

    node = dict(kind=enumerator, school="", race="", min=0.0, all=True, txt=(0, 0), child=(0, 0))
    if kind in ("any", "all"):
        if not rule["rules"]:
            raise SystemExit("%s has an empty %r rule, which is vacuously %s"
                             % (where, kind, "false" if kind == "any" else "true"))
        kids = [flatten_rule(r, nodes, texts, children, base, where) for r in rule["rules"]]
        begin = len(children)
        # RELATIVE to this tree's own first node, because a god's rule is a slice of the
        # shared node array and an absolute index would read another god's rule the
        # moment a slice did not start at zero.
        children.extend(k - base for k in kids)
        node["child"] = (begin, len(children))
    elif kind == "school":
        node["school"] = rule["school"]
    elif kind == "race":
        node["race"] = rule["race"]
    elif kind in ("ocean_share", "ruins", "races", "primordial_count"):
        node["min"] = float(rule["min"])
    elif kind == "civilization_city":
        node["txt"] = texts.add(rule["ids"])
    elif kind == "nest_family":
        node["txt"] = texts.add(rule["families"])
    elif kind == "gods":
        node["txt"] = texts.add(rule["ids"])
        node["all"] = bool(rule.get("all", True))

    nodes[index] = node
    return index


def rule_node_literal(n):
    return ("{RuleKind::%s, %s, %s, %s, %s, %du, %du, %du, %du}"
            % (n["kind"], cpp_string(n["school"]), cpp_string(n["race"]), cpp_double(n["min"]),
               "true" if n["all"] else "false",
               n["txt"][0], n["txt"][0] + n["txt"][1],
               n["child"][0], n["child"][1]))


def walk_rules(rule):
    yield rule
    for inner in rule.get("rules", []):
        yield from walk_rules(inner)


def load_pantheon():
    doc = load("pantheon.json")

    known_schools = [s["name"] for s in load("magic-schools.json")["schools"] if not s["hidden"]]
    hidden_schools = [s["name"] for s in load("magic-schools.json")["schools"] if s["hidden"]]
    races = {r["id"] for r in load("registry-sections.json")["parent_races"]}
    civilizations = [p["id"] for p in load("civilizations.json")["profiles"] if p["kind"] == "entity"]
    families = {p["family"] for p in load("nest-profiles.json")["profiles"]}

    gods = doc["gods"]
    ids = [g["id"] for g in gods]
    if len(set(ids)) != len(ids):
        raise SystemExit("pantheon.json has a duplicate god id")
    known = set(ids)

    school_gods = [g for g in gods if g["family"] == "school"]
    if sorted(g["exists"]["school"] for g in school_gods) != sorted(known_schools):
        raise SystemExit("the school gods must cover every known school exactly once; got %s"
                         % sorted(g["exists"].get("school") for g in school_gods))

    for god in gods:
        if god["family"] not in GOD_FAMILIES:
            raise SystemExit("%s has unknown family %r" % (god["id"], god["family"]))
        if "sovereign" not in god["aspects"]:
            raise SystemExit("%s needs a sovereign aspect: it is the name every faith falls "
                             "back to" % god["id"])
        if set(god["aspects"]) - {"sovereign", "wild"}:
            raise SystemExit("%s has an aspect nothing can select" % god["id"])
        if not god.get("school_affinity"):
            raise SystemExit("%s needs school affinities" % god["id"])
        for s in god["school_affinity"]:
            # `moon` is deliberately in this list and is NOT a school: it names no potency
            # grid, so it contributes exactly zero to faith weighting. Do not "fix" it.
            if s not in known_schools and s != "moon":
                raise SystemExit("%s has affinity for %r, which is neither a known school "
                                 "nor the pseudo-school 'moon'" % (god["id"], s))
        for key in ("allies", "rivals"):
            unknown = set(god.get(key, [])) - known
            if unknown:
                raise SystemExit("%s names unknown god(s) %s in %s"
                                 % (god["id"], sorted(unknown), key))
        if "folds_into" in god and god["folds_into"] not in known:
            raise SystemExit("%s folds into an unknown god" % god["id"])
        if ("race" in god) != (god["family"] == "ancestor"):
            raise SystemExit("%s: `race` and family 'ancestor' go together" % god["id"])
        if "race" in god and god["race"] not in races:
            raise SystemExit("%s is the ancestor of %r, which is not a parent race in "
                             "registry-sections.json" % (god["id"], god["race"]))
        for civ in god.get("epithets", {}):
            if civ not in civilizations:
                raise SystemExit("%s has an epithet for %r, which is not a civilization"
                                 % (god["id"], civ))
        for fam in god.get("purge_families", []):
            if fam not in families:
                raise SystemExit("%s purges %r, which is not a nest family" % (god["id"], fam))
        for rule in walk_rules(god["exists"]):
            if rule.get("kind") == "school" and rule["school"] not in known_schools:
                raise SystemExit("%s exists on unknown school %r" % (god["id"], rule["school"]))
            if rule.get("kind") == "race" and rule["race"] not in races:
                raise SystemExit("%s exists on unknown parent race %r" % (god["id"], rule["race"]))
            if rule.get("kind") == "civilization_city":
                unknown = set(rule["ids"]) - set(civilizations)
                if unknown:
                    raise SystemExit("%s names unknown civilization(s) %s"
                                     % (god["id"], sorted(unknown)))
            if rule.get("kind") == "nest_family":
                unknown = set(rule["families"]) - families
                if unknown:
                    raise SystemExit("%s names unknown nest famil(ies) %s"
                                     % (god["id"], sorted(unknown)))

    cosmologies = doc["cosmologies"]
    cosmology_ids = [c["id"] for c in cosmologies]
    if len(set(cosmology_ids)) != len(cosmology_ids):
        raise SystemExit("pantheon.json has a duplicate cosmology id")
    for cosmology in cosmologies:
        unknown = set(cosmology.get("high", [])) - known
        if unknown:
            raise SystemExit("%s names unknown high god(s) %s" % (cosmology["id"], sorted(unknown)))
        fact = cosmology["weight"].get("fact", "")
        if fact and fact not in WEIGHT_FACTS:
            raise SystemExit("%s weighs on %r, which is not a numeric world fact (%s)"
                             % (cosmology["id"], fact, "unknown" if fact not in FACT_NAMES
                                else "not a number"))
        if fact and "per" not in cosmology["weight"]:
            raise SystemExit("%s names a fact but has no `per`, so the fact does nothing"
                             % cosmology["id"])
        for rule in walk_rules(cosmology["requires"]):
            if rule.get("kind") == "gods":
                unknown = set(rule["ids"]) - known
                if unknown:
                    raise SystemExit("%s requires unknown god(s) %s"
                                     % (cosmology["id"], sorted(unknown)))
            if rule.get("kind") == "school" and rule["school"] not in known_schools:
                raise SystemExit("%s requires unknown school %r" % (cosmology["id"], rule["school"]))

    # `choose_cosmology` falls back to this id by name when nothing is eligible. The
    # reference raises StopIteration if it is gone; there is no second fallback.
    fallback = "cos_mosaic"
    if fallback not in cosmology_ids:
        raise SystemExit("%s is the hardcoded fallback cosmology and must exist" % fallback)
    unknown = set(doc["fold_free_cosmologies"]) - set(cosmology_ids)
    if unknown:
        raise SystemExit("fold_free_cosmologies names unknown cosmolog(ies) %s" % sorted(unknown))

    if sorted(doc["affinities"]) != sorted(civilizations):
        raise SystemExit("affinities must cover every civilization exactly; missing %s, extra %s"
                         % (sorted(set(civilizations) - set(doc["affinities"])),
                            sorted(set(doc["affinities"]) - set(civilizations))))
    for civ, table in doc["affinities"].items():
        extra = set(table) - {"strong", "mild", "aversion"}
        if extra:
            raise SystemExit("%s affinity has key(s) %s that nothing scores" % (civ, sorted(extra)))
        for key in ("strong", "mild", "aversion"):
            unknown = set(table.get(key, [])) - known
            if unknown:
                raise SystemExit("%s affinity names unknown god(s) %s" % (civ, sorted(unknown)))
        # Cumulative, not exclusive: the reference ADDS every matching bonus, so a god in
        # both `strong` and `aversion` scores zero and silently leaves the pool.
        counts = [g for key in ("strong", "mild", "aversion") for g in table.get(key, [])]
        if len(set(counts)) != len(counts):
            raise SystemExit("%s affinity lists a god twice; the bonuses add rather than "
                             "select, so the god scores the sum" % civ)

    for family, god_id in doc["fear_gods"].items():
        if family not in families:
            raise SystemExit("fear_gods keys are nest families; %r is not one" % family)
        if god_id not in known:
            raise SystemExit("fear_gods names unknown god %r" % god_id)
    for god_id, pushed in doc["wild_push_families"].items():
        if god_id not in known:
            raise SystemExit("wild_push_families names unknown god %r" % god_id)
        if "wild" not in next(g for g in gods if g["id"] == god_id)["aspects"]:
            raise SystemExit("%s is pushed toward a wild aspect it does not have" % god_id)
        unknown = set(pushed) - families
        if unknown:
            raise SystemExit("wild_push_families names unknown nest famil(ies) %s" % sorted(unknown))

    hidden = doc["hidden_gods"]
    hidden_ids = [g["id"] for g in hidden]
    if len(set(hidden_ids)) != len(hidden_ids):
        raise SystemExit("hidden_gods has a duplicate id")
    if known & set(hidden_ids):
        raise SystemExit("a god is in both halves of the catalogue: %s"
                         % sorted(known & set(hidden_ids)))
    for god in hidden:
        if god["family"] != "outside":
            raise SystemExit("%s must have family 'outside'; terrain_nomads finds the hidden "
                             "four by that string alone" % god["id"])
        # `school` SINGULAR, and a hidden one. A known god carries `school_affinity`, a
        # list; the export maps this one field into a one-element `schools` list.
        if god.get("school") not in hidden_schools:
            raise SystemExit("%s must name one hidden school, got %r"
                             % (god["id"], god.get("school")))
        if "school_affinity" in god:
            raise SystemExit("%s carries school_affinity; hidden gods carry `school`" % god["id"])
        if set(god["aspects"]) != {"sovereign", "wild"}:
            raise SystemExit("%s needs both aspects: a revelation pushes it Wild" % god["id"])
        if not 0 < god["noticing"] <= god["climb"]:
            raise SystemExit("%s: noticing must be a positive floor at or below climb" % god["id"])
        unknown = set(god.get("rivals", [])) - known
        if unknown:
            raise SystemExit("%s names unknown rival(s) %s" % (god["id"], sorted(unknown)))
        for fam in god.get("purge_families", []):
            if fam not in families:
                raise SystemExit("%s purges %r, which is not a nest family" % (god["id"], fam))
        for rule in walk_rules(god["exists"]):
            if rule.get("kind") not in RULE_KINDS:
                raise SystemExit("%s uses unknown existence rule %r" % (god["id"], rule.get("kind")))

    for key in KNOWN_KEYS + HIDDEN_KEYS:
        if key not in doc:
            raise SystemExit("pantheon.json is missing %r, which the identity is taken over" % key)
    return doc, civilizations


def render_pantheon():
    doc, civilizations = load_pantheon()
    gods = doc["gods"]

    words = Pool()       # domains, allies, rivals, affinities, high gods, families
    texts = Pool()       # the membership pools an existence rule tests against
    nodes, children = [], []
    epithets = []

    def rule_span(rule, where):
        base = len(nodes)
        flatten_rule(rule, nodes, texts, children, base, where)
        return base, len(nodes) - base

    god_rows = []
    for god in gods:
        row = dict(
            id=god["id"], name=god["name"], family=god["family"],
            sovereign=god["aspects"]["sovereign"], wild=god["aspects"].get("wild", ""),
            domains=words.add(god["domains"]),
            allies=words.add(god.get("allies", [])),
            rivals=words.add(god.get("rivals", [])),
            school_affinity=words.add(god["school_affinity"]),
            purge=words.add(god.get("purge_families", [])),
            smite=god.get("smite", ""), race=god.get("race", ""),
            folds_into=god.get("folds_into", ""), festival=god.get("festival", ""),
        )
        begin = len(epithets)
        # Civilization order, not the JSON's, so a re-ordered epithet block is the same
        # catalogue: nothing reads these positionally, only by civilization id.
        for civ in civilizations:
            if civ in god.get("epithets", {}):
                epithets.append((civ, god["epithets"][civ]))
        row["epithets"] = (begin, len(epithets) - begin)
        row["exists"] = rule_span(god["exists"], god["id"])
        god_rows.append(row)

    cosmology_rows = []
    for c in doc["cosmologies"]:
        cosmology_rows.append(dict(
            id=c["id"], name=c["name"], arrangement=c["arrangement"],
            high=words.add(c.get("high", [])),
            fact=c["weight"].get("fact", ""),
            base=float(c["weight"].get("base", 0.0)), per=float(c["weight"].get("per", 0.0)),
            requirement=rule_span(c["requires"], c["id"]),
        ))

    affinity_rows = []
    for civ in civilizations:
        table = doc["affinities"][civ]
        affinity_rows.append(dict(id=civ, strong=words.add(table.get("strong", [])),
                                  mild=words.add(table.get("mild", [])),
                                  aversion=words.add(table.get("aversion", []))))

    hidden_rows = []
    for god in doc["hidden_gods"]:
        hidden_rows.append(dict(
            id=god["id"], name=god["name"], school=god["school"],
            sovereign=god["aspects"]["sovereign"], wild=god["aspects"]["wild"],
            domains=words.add(god["domains"]), rivals=words.add(god.get("rivals", [])),
            purge=words.add(god.get("purge_families", [])),
            interest=god["interest"], noticing=god["noticing"], climb=god["climb"],
            smite=god.get("smite", ""), exists=rule_span(god["exists"], god["id"]),
        ))

    fold_free = words.add(doc["fold_free_cosmologies"])
    # Sorted so the tables read the same way whatever order the JSON object was written
    # in; both are looked up by key, never by position.
    fear = sorted(doc["fear_gods"].items())
    push = [(god_id, words.add(doc["wild_push_families"][god_id]))
            for god_id in sorted(doc["wild_push_families"])]

    known_digest = hashlib.sha256(_canonical({k: doc[k] for k in KNOWN_KEYS})).hexdigest()
    hidden_digest = hashlib.sha256(_canonical({k: doc[k] for k in HIDDEN_KEYS})).hexdigest()

    lines = [BANNER.format(source="pantheon.json")]
    lines.append('#include "fwg/religion.hpp"\n')
    lines.append("namespace fwg {\nnamespace {\n")

    lines.append(words.literal("kWords"))
    lines.append(texts.literal("kRuleTexts"))
    lines.append("constexpr std::uint32_t kRuleChildren[] = {%s};\n"
                 % (", ".join("%du" % c for c in children) if children else "0u"))

    lines.append("// Existence rules, flattened. Each god and cosmology owns a contiguous slice")
    lines.append("// whose root is its first node; a combinator's children are INDICES into")
    lines.append("// kRuleChildren, relative to that slice.")
    lines.append("constexpr RuleNode kRules[] = {")
    for n in nodes:
        lines.append("    " + rule_node_literal(n) + ",")
    lines.append("};\n")

    lines.append("constexpr Epithet kEpithets[] = {%s};\n"
                 % (", ".join("{%s, %s}" % (cpp_string(c), cpp_string(n)) for c, n in epithets)
                    if epithets else "{}"))

    def span(pair, pool="kWords"):
        return "{%s + %d, %d}" % (pool, pair[0], pair[1])

    lines.append("// ORDER IS A CONTRACT. resolve_gods emits one record per row in this order,")
    lines.append("// and choose_faiths builds its weighted draw by walking the resolved records")
    lines.append("// in the same order -- so this table decides ties in every faith's draw.")
    lines.append("// Reordering it is a different world on the same seed.")
    lines.append("constexpr PantheonGod kGods[] = {")
    for r in god_rows:
        lines.append("    {%s, %s, %s, {%s, %s}," % (cpp_string(r["id"]), cpp_string(r["name"]),
                                                     cpp_string(r["family"]),
                                                     cpp_string(r["sovereign"]), cpp_string(r["wild"])))
        lines.append("     %s, {kRules + %d, %d}, %s, %s, %s, %s,"
                     % (span(r["domains"]), r["exists"][0], r["exists"][1], span(r["allies"]),
                        span(r["rivals"]), span(r["school_affinity"]), span(r["purge"])))
        lines.append("     %s, %s, %s, %s, %s},"
                     % (cpp_string(r["smite"]), cpp_string(r["race"]),
                        span(r["epithets"], "kEpithets"), cpp_string(r["folds_into"]),
                        cpp_string(r["festival"])))
    lines.append("};\n")

    lines.append("// ORDER IS A CONTRACT. choose_cosmology walks this table in order, building")
    lines.append("// `eligible`, and subtracts from ONE uniform draw along it.")
    lines.append("constexpr Cosmology kCosmologies[] = {")
    for r in cosmology_rows:
        lines.append("    {%s, %s, {kRules + %d, %d}, {%s, %s, %s}, %s, %s},"
                     % (cpp_string(r["id"]), cpp_string(r["name"]),
                        r["requirement"][0], r["requirement"][1],
                        cpp_string(r["fact"]), cpp_double(r["base"]), cpp_double(r["per"]),
                        span(r["high"]), cpp_string(r["arrangement"])))
    lines.append("};\n")

    lines.append("constexpr AffinityTable kAffinities[] = {")
    for r in affinity_rows:
        lines.append("    {%s, %s, %s, %s},"
                     % (cpp_string(r["id"]), span(r["strong"]), span(r["mild"]), span(r["aversion"])))
    lines.append("};\n")

    lines.append("// ORDER IS A CONTRACT: the last four entries of religion.gods, in this order.")
    lines.append("constexpr HiddenGod kHiddenGods[] = {")
    for r in hidden_rows:
        lines.append("    {%s, %s, %s, {%s, %s},"
                     % (cpp_string(r["id"]), cpp_string(r["name"]), cpp_string(r["school"]),
                        cpp_string(r["sovereign"]), cpp_string(r["wild"])))
        lines.append("     %s, {kRules + %d, %d}, %s, %d, %d, %s, %s, %s},"
                     % (span(r["domains"]), r["exists"][0], r["exists"][1],
                        cpp_string(r["interest"]), r["noticing"], r["climb"],
                        span(r["rivals"]), span(r["purge"]), cpp_string(r["smite"])))
    lines.append("};\n")

    lines.append("constexpr std::string_view kFoldFree[] = {%s};"
                 % ", ".join(cpp_string(v) for v in doc["fold_free_cosmologies"]))
    lines.append("constexpr FearGod kFearGods[] = {%s};"
                 % ", ".join("{%s, %s}" % (cpp_string(f), cpp_string(g)) for f, g in fear))
    lines.append("constexpr WildPush kWildPush[] = {%s};\n"
                 % ", ".join("{%s, %s}" % (cpp_string(g), span(p)) for g, p in push))

    lines.append("}  // namespace\n")
    lines.append("std::span<const PantheonGod> pantheon_gods() { return kGods; }")
    lines.append("std::span<const HiddenGod> hidden_pantheon_gods() { return kHiddenGods; }")
    lines.append("std::span<const Cosmology> pantheon_cosmologies() { return kCosmologies; }")
    lines.append("std::span<const AffinityTable> pantheon_affinities() { return kAffinities; }")
    lines.append("std::span<const std::string_view> fold_free_cosmologies() { return kFoldFree; }")
    lines.append("std::span<const FearGod> fear_gods() { return kFearGods; }")
    lines.append("std::span<const WildPush> wild_push_families() { return kWildPush; }")
    lines.append("RulePools pantheon_rule_pools() { return {kRuleChildren, kRuleTexts}; }\n")

    weights = doc["affinity_weights"]
    lines.append("PantheonTuning pantheon_tuning() {")
    lines.append("    PantheonTuning t;")
    lines.append("    t.aspect_midpoint = %s;" % cpp_double(doc["aspect_midpoint"]))
    lines.append("    t.nest_aspect_push = %s;" % cpp_double(doc["nest_aspect_push"]))
    for key in ("strong", "mild", "aversion", "high", "civic_base"):
        lines.append("    t.affinity.%s = %s;" % (key, cpp_double(weights[key])))
    lines.append("    return t;")
    lines.append("}\n")

    src = doc["source_identity"]
    hidden_src = doc["hidden_source_identity"]
    lines.append("// THE SCOPE OF THIS DIGEST IS THE POINT. It is taken over the known catalogue")
    lines.append("// alone, so editing one of the four Outside gods does not retire every saved")
    lines.append("// world -- which is exactly why the reference kept them in a second file.")
    lines.append("PantheonIdentity pantheon_identity() {")
    lines.append("    return {%d, %d, %s, %s};" % (src["schema_version"], src["revision"],
                                                   cpp_string(src["sha256"]), cpp_string(known_digest)))
    lines.append("}\n")
    lines.append("PantheonIdentity hidden_pantheon_identity() {")
    lines.append("    return {%d, %d, %s, %s};" % (hidden_src["schema_version"], hidden_src["revision"],
                                                   cpp_string(hidden_src["sha256"]),
                                                   cpp_string(hidden_digest)))
    lines.append("}\n")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_pantheon.cpp": render_pantheon}
