"""The heritage tables: key traits, derivation, lexicon, overrides, appearance.

Eight data files become one generated translation unit. Heritage is 84 KB of authored
JSON over 40 lines of glue, so most of this module is validation rather than emission --
five of the reference's linters move HERE, because they police fixed data and a fixed
table can be checked once at build time instead of on every call:

  * a derivation map must cover every value of the axis it reads. Emitted as an ARRAY
    INDEXED BY THE AXIS ENUM, so an incomplete map cannot be represented at all.
  * a culture rule may not map to a number or a bool; a genome rule must be from/map.
  * an appearance key must have the shape its block declares -- band, palette, token,
    token list, prose, {form,note}, {onset_years,height_fraction,read}.
  * a palette's weights are population frequencies and must sum to 1.
  * a lexicon root must be spellable in the phonemes its own race's soma admits.

What stays in C++ is everything that can fire on a CALL rather than on the data: an
unknown parent race, a delta that restates what it inherits, an override that restates
a derived value, and the cross-layer appearance envelopes -- all of which depend on the
(civ_id, parent_race_id) pair the caller supplies.

Three tightenings against the reference are deliberate and are listed in
docs/port-specs/22-heritage.md's open questions:
  * a rule must be EXACTLY one of copy / craft / from+map. The reference tested `copy`
    first, so {"copy": ..., "map": ...} passed as a copy with the map ignored.
  * a genome rule must be from+map, because each one lands in a typed field.
  * `drop_roots` must name a slot the family actually holds. The reference read the
    field and never validated it, so a typo dropped nothing and said nothing.
"""
import re

from embed.common import BANNER, OUT, cpp_double, cpp_string, load

# The five packaged policy documents, in the order their revisions are published.
POLICY_FILES = ("traits", "derivation", "lexicon", "overrides", "appearance")

SOURCES = ("heritage-axes.json", "heritage-phonology.json", "heritage-name-draw.json",
           "heritage-traits.json", "heritage-derivation.json", "heritage-lexicon.json",
           "heritage-overrides.json", "heritage-appearance.json")

TOKEN = re.compile(r"^[a-z][a-z0-9_]*$")
HEX_COLOUR = re.compile(r"^#[0-9a-f]{6}$")


def die(message):
    raise SystemExit("heritage: " + message)


def is_number(value):
    return not isinstance(value, bool) and isinstance(value, (int, float))


def canonical_number(value):
    """The exact text Python's json.dumps writes for this number.

    The identity digest is a digest of that text, so `350` and `350.0` are the same
    quantity and a different world document. The spelling travels with the value into
    C++ rather than being reconstructed there, because two shortest-round-trip printers
    agree on the digits and not on where a trailing `.0` goes.
    """
    if not is_number(value):
        die("%r is not a number" % (value,))
    return repr(value)


# ---------------------------------------------------------------------------
# Emission helpers
# ---------------------------------------------------------------------------

class Arrays(object):
    """A pool of named constexpr arrays, deduplicated by content.

    Every span in the generated tables points at one of these. Deduplicating matters
    more than it looks: the fourteen forbidden forms are copied into all twelve genomes
    and the same craft pair is shared by several peoples.
    """

    def __init__(self):
        self.lines = []
        self.cache = {}
        self.count = 0

    def literals(self, type_name, literals):
        """A named array of already-rendered initialisers, or `{}` for an empty span."""
        key = (type_name, tuple(literals))
        if not literals:
            return "{}"
        if key in self.cache:
            return self.cache[key]
        name = "kPool%d" % self.count
        self.count += 1
        self.lines.append("constexpr %s %s[] = {%s};" % (type_name, name, ", ".join(literals)))
        self.cache[key] = name
        return name

    def strings(self, values):
        return self.literals("std::string_view", [cpp_string(v) for v in values])

    def bools(self, values):
        return self.literals("bool", ["true" if v else "false" for v in values])

    def numbers(self, values):
        return self.literals("Number",
                             ["{%s, %s}" % (cpp_double(v), cpp_string(canonical_number(v)))
                              for v in values])

    def doubles(self, values):
        return self.literals("double", [cpp_double(v) for v in values])

    def morphemes(self, pairs):
        return self.literals("Morpheme",
                             ["{%s, %s}" % (cpp_string(slot), cpp_string(root))
                              for slot, root in pairs])


# ---------------------------------------------------------------------------
# Phonemes, in Python, because two lints need an inventory before any C++ exists
# ---------------------------------------------------------------------------

def inventory(phonology, axes, traits):
    """(consonants, vowels, forbidden) in ALPHABET order, not gate-firing order."""
    kept = set(phonology["core_consonants"]) | set(phonology["core_vowels"])
    banned = set()
    for axis in phonology["gated_axes"]:
        rule = phonology["gates"][axis].get(traits[axis], {})
        kept.update(rule.get("add", ()))
        banned.update(rule.get("drop", ()))
    kept -= banned
    consonants = phonology["alphabet"]["consonants"]
    vowels = phonology["alphabet"]["vowels"]
    alphabet = set(consonants) | set(vowels)
    stray = sorted((kept | banned) - alphabet)
    if stray:
        die("an articulatory gate produced phonemes outside the alphabet: %s" % stray)
    if set(phonology["gated_axes"]) - set(axes):
        die("a gated axis is not a heritage axis")
    return ([p for p in consonants if p in kept],
            [p for p in vowels if p in kept],
            [p for p in consonants + vowels if p in banned])


# ---------------------------------------------------------------------------
# Validation -- the linters that police fixed data
# ---------------------------------------------------------------------------

def check_axis(axes, craft_count, crafts, hidden_schools, axis, value, label):
    if axis not in axes:
        die("%s names an unknown axis %r" % (label, axis))
    if axis == "craft_focus":
        if not isinstance(value, list) or len(value) != craft_count or len(set(value)) != craft_count:
            die("%s needs exactly %d distinct crafts" % (label, craft_count))
        for item in value:
            if item not in crafts:
                die("%s craft %r is not a craft" % (label, item))
        return
    if is_number(value) or isinstance(value, bool):
        die("heritage traits carry no numbers; %s axis %r must be an enum value" % (label, axis))
    if value not in axes[axis]:
        if axis == "magic_school" and value in hidden_schools:
            die("%s claims the hidden school %r; generation can never raise one" % (label, value))
        die("%s axis %r is not one of %s" % (label, axis, sorted(axes[axis])))


def lint_traits(traits, meta):
    axes = meta["axes"]
    for race, base in traits["races"].items():
        if set(base) != set(axes):
            missing = sorted(set(axes) - set(base)) or sorted(set(base) - set(axes))
            die("race %r must name exactly the %d heritage axes; check %s"
                % (race, len(axes), missing))
        for axis, value in base.items():
            check_axis(axes, meta["craft_count"], meta["crafts"], meta["hidden_schools"],
                       axis, value, "race %r" % race)
    for cid, delta in traits["peoples"].items():
        # A delta is NOT required to be complete; that is the whole point of a delta.
        for axis, value in delta.items():
            check_axis(axes, meta["craft_count"], meta["crafts"], meta["hidden_schools"],
                       axis, value, "people %r" % cid)


def lint_rule(axes, craft_count, section, path, rule):
    """One derivation rule, tightened to exactly one of the three shapes."""
    if not isinstance(rule, dict):
        die("derivation rule at %s must be copy, from/map or craft" % path)
    shapes = [name for name in ("copy", "craft", "from") if name in rule]
    if len(shapes) != 1 or set(rule) - {"copy", "craft", "from", "map"}:
        die("derivation rule at %s must be exactly one of copy, craft or from/map" % path)
    shape = shapes[0]
    if shape == "copy":
        if "map" in rule:
            die("derivation rule at %s carries both a copy and a map; one of them does "
                "nothing and nobody can tell which" % path)
        if rule["copy"] not in axes:
            die("derivation rule at %s copies unknown axis %r" % (path, rule["copy"]))
        return
    if shape == "craft":
        if "map" in rule:
            die("derivation rule at %s carries both a craft index and a map" % path)
        if rule["craft"] not in range(craft_count):
            die("derivation rule at %s indexes craft_focus out of range" % path)
        return
    axis = rule["from"]
    if axis not in axes or axis == "craft_focus":
        die("derivation rule at %s reads unknown axis %r" % (path, axis))
    table = rule.get("map")
    if not isinstance(table, dict):
        die("derivation rule at %s needs a map" % path)
    for value in axes[axis]:
        if value not in table:
            die("derivation has no rule for %r/%r; every enum value must map" % (axis, value))
    extra = sorted(set(table) - set(axes[axis]))
    if extra:
        die("derivation rule at %s maps values outside %r: %s" % (path, axis, extra))
    for mapped in table.values():
        if isinstance(mapped, (dict, list)):
            die("derivation nests %s more than two levels deep" % path)
        if section == "culture" and (isinstance(mapped, bool) or is_number(mapped)):
            die("heritage culture carries no numbers; %s must be an enum value" % path)


def lint_derivation(derivation, meta):
    axes = meta["axes"]
    blocks = meta["culture_blocks"]
    if set(derivation["culture"]) != set(blocks):
        die("derivation must produce exactly the culture blocks %s" % sorted(blocks))
    for block, keys in blocks.items():
        if set(derivation["culture"][block]) != set(keys):
            die("derivation culture block %r must produce exactly %s" % (block, sorted(keys)))
    for section in ("culture", "genome"):
        for block, rules in derivation[section].items():
            for key, rule in rules.items():
                lint_rule(axes, meta["craft_count"], section,
                          "%s.%s.%s" % (section, block, key), rule)
    if set(derivation["genome"]) != set(meta["genome_derived_blocks"]):
        die("derivation must produce exactly the genome blocks %s"
            % sorted(meta["genome_derived_blocks"]))
    for key in meta["rate_keys"]:
        for value in derivation["genome"]["rates"][key]["map"].values():
            if not is_number(value) or not 0 <= value <= 1:
                die("derivation rate %r must be a number in 0..1" % key)
    # Every genome rule lands in a typed field, and only from/map carries one.
    for block, rules in derivation["genome"].items():
        for key, rule in rules.items():
            if "from" not in rule:
                die("genome rule %s.%s is not a from/map rule; it has no typed home in "
                    "the port, so give it one before authoring it" % (block, key))


def lint_lexicon(lexicon, traits, phonology, meta):
    shared = set(lexicon["shared_slots"])
    seen_branches = set()
    claimed_races = set()
    for family, entry in lexicon["families"].items():
        race = entry["parent_race"]
        if race not in traits["races"]:
            die("lexicon family %r names unknown race %r" % (family, race))
        if race in claimed_races:
            die("lexicon families %r and an earlier one both claim parent race %r; the "
                "family lookup returns the first match and nothing else would notice"
                % (family, race))
        claimed_races.add(race)
        missing = sorted(shared - set(entry["roots"]))
        if missing:
            die("lexicon family %r is missing shared slots %s" % (family, missing))
        consonants, vowels, _ = inventory(phonology, meta["axes"], traits["races"][race])
        allowed = set("".join(consonants)) | set("".join(vowels))
        for slot, root in entry["roots"].items():
            if not root or not root.isascii() or root != root.strip():
                die("lexicon family %r root %r is not a bare token" % (family, slot))
            stray = sorted(set(root) - allowed)
            if stray:
                die("lexicon family %r root %r=%r uses letters its soma cannot make: %s"
                    % (family, slot, root, stray))
        for branch, spec in entry["branches"].items():
            if branch in seen_branches:
                die("lexicon branch %r is declared twice" % branch)
            seen_branches.add(branch)
            for change in spec.get("sound_changes", []):
                if not isinstance(change, list) or len(change) != 2 or not change[0]:
                    die("lexicon branch %r has a malformed sound change" % branch)
            # The reference read drop_roots and never checked it, so a typo dropped
            # nothing and reported nothing. No branch uses it today, which is exactly
            # why the fault would have shipped.
            for slot in spec.get("drop_roots", []):
                if slot not in entry["roots"]:
                    die("lexicon branch %r drops slot %r its family never had" % (branch, slot))
    if claimed_races != set(traits["races"]):
        die("every parent race needs a language family; %s has none"
            % sorted(set(traits["races"]) - claimed_races))
    for role, slots in lexicon["slot_roles"].items():
        if not slots:
            die("lexicon slot role %r is empty" % role)
    for kind in ("personal_templates", "settlement_templates"):
        for template in lexicon[kind]:
            if len(template["pattern"]) != 2:
                die("%s entry %r is not a pair of roles" % (kind, template["pattern"]))
            for role in template["pattern"]:
                if role not in lexicon["slot_roles"]:
                    die("%s names unknown role %r" % (kind, role))
            if not isinstance(template["weight"], int) or template["weight"] <= 0:
                die("%s entry %r needs a positive integer weight" % (kind, template["pattern"]))


def lint_overrides(overrides, traits, meta):
    blocks = meta["culture_blocks"]
    for section in ("culture", "genome"):
        for cid in overrides.get(section, {}):
            if cid not in traits["peoples"]:
                die("override names %r, which is not a people in traits" % cid)
    for cid, patch in overrides.get("culture", {}).items():
        for block, keys in patch.items():
            if block not in blocks:
                die("override for %r touches unknown culture block %r" % (cid, block))
            for key, value in keys.items():
                if key not in blocks[block]:
                    die("override for %r touches unknown culture key %s.%s" % (cid, block, key))
                if isinstance(value, (dict, list)):
                    die("override for %r nests %s.%s more than two levels deep"
                        % (cid, block, key))
                if isinstance(value, bool) or is_number(value):
                    die("heritage culture carries no numbers; %s %s.%s must be an enum value"
                        % (cid, block, key))
    # The reference checked only the civ id under `genome`: no block, no key, no shape.
    # A genome override lands in a typed field here, so the shape is checked now.
    for cid, patch in overrides.get("genome", {}).items():
        for block, keys in patch.items():
            if not isinstance(keys, dict):
                die("genome override for %r block %r must map keys to values" % (cid, block))
            for key, value in keys.items():
                if isinstance(value, dict):
                    if set(value) - {"add", "drop"}:
                        die("genome override %s %s.%s must be an add/drop patch"
                            % (cid, block, key))
                elif isinstance(value, list):
                    die("genome override %s %s.%s nests more than two levels deep"
                        % (cid, block, key))


# --- appearance -------------------------------------------------------------

def prose(value, label, minimum):
    if not isinstance(value, str) or len(value.strip()) < minimum:
        die("%s must be open English a concept artist can act on, not %r" % (label, value))


def token(value, label):
    if not isinstance(value, str) or not TOKEN.match(value):
        die("%s must be a bare lowercase token, not %r" % (label, value))


def token_list(value, label):
    if not isinstance(value, list) or not value:
        die("%s must be a non-empty list of tokens" % label)
    if len(set(value)) != len(value):
        die("%s names the same entry twice" % label)
    for item in value:
        token(item, label)


def bounded(value, label, low, high):
    if not is_number(value):
        die("%s must be a number" % label)
    if value != value or value in (float("inf"), float("-inf")):
        die("%s must be a finite number" % label)
    if not low <= value <= high:
        die("%s is %g, outside %g..%g" % (label, value, low, high))


def band(value, label, keys):
    if not isinstance(value, dict) or set(value) != set(keys):
        die("%s must name exactly %s" % (label, list(keys)))
    for key in keys:
        if not is_number(value[key]):
            die("%s.%s must be a number" % (label, key))
    if not 0 < value["min"] <= value["mean"] <= value["max"]:
        die("%s needs 0 < min <= mean <= max; got %g/%g/%g"
            % (label, value["min"], value["mean"], value["max"]))
    if value["sd"] <= 0:
        die("%s.sd must be positive; a per-NPC draw needs a spread" % label)


def palette(value, label, meta):
    minimum = meta["validation"]["palette_min_swatches"]
    if not isinstance(value, list) or len(value) < minimum:
        die("%s needs at least %d swatches" % (label, minimum))
    names, total = [], 0.0
    for index, swatch in enumerate(value):
        where = "%s[%d]" % (label, index)
        if not isinstance(swatch, dict) or set(swatch) != {"name", "hex", "weight"}:
            die("%s must name exactly name, hex and weight" % where)
        token(swatch["name"], where + ".name")
        if not isinstance(swatch["hex"], str) or not HEX_COLOUR.match(swatch["hex"]):
            die("%s.hex must be lowercase #rrggbb, not %r" % (where, swatch["hex"]))
        if not is_number(swatch["weight"]) or swatch["weight"] <= 0:
            die("%s.weight must be positive; drop the swatch instead" % where)
        names.append(swatch["name"])
        total += swatch["weight"]
    if len(set(names)) != len(names):
        die("%s names the same swatch twice" % label)
    if abs(total - 1.0) > meta["validation"]["weight_tolerance"]:
        die("%s weights are population frequencies and must sum to 1; they sum to %g"
            % (label, total))


def check_appearance_key(block, key, value, label, meta):
    where = "%s %s.%s" % (label, block, key)
    v = meta["validation"]
    if block == "frame":
        if key in ("height_m", "mass_kg"):
            if not isinstance(value, dict) or set(value) != set(meta["sexes"]):
                die("%s must name exactly %s" % (where, meta["sexes"]))
            for sex in meta["sexes"]:
                band(value[sex], "%s.%s" % (where, sex), meta["band_keys"])
        elif key == "sexual_dimorphism":
            if value not in meta["sexual_dimorphism"]:
                die("%s is not one of %s" % (where, sorted(meta["sexual_dimorphism"])))
        else:
            token(value, where)
    elif block == "proportion":
        if key == "heads_tall":
            bounded(value, where, *v["heads_tall_range"])
        else:
            bounded(value, where, *v["body_ratio_range"])
    elif block == "coloration":
        if not isinstance(value, dict) or set(value) != set(meta["palette_keys"]):
            die("%s must name exactly %s" % (where, meta["palette_keys"]))
        prose(value["note"], where + ".note", v["prose_min_chars"])
        palette(value["swatches"], where + ".swatches", meta)
    elif block == "grooming":
        if key == "common_styles":
            token_list(value, where)
        elif key == "facial_hair":
            if value not in meta["facial_hair"]:
                die("%s is not one of %s" % (where, sorted(meta["facial_hair"])))
        else:
            token(value, where)
    elif block == "life_stages":
        if key == "max_years":
            bounded(value, where, *v["max_years_range"])
        else:
            if not isinstance(value, dict) or set(value) != set(meta["stage_keys"]):
                die("%s must name exactly %s" % (where, meta["stage_keys"]))
            bounded(value["onset_years"], where + ".onset_years", *v["onset_years_range"])
            bounded(value["height_fraction"], where + ".height_fraction",
                    *v["height_fraction_range"])
            if not isinstance(value["read"], str) or not value["read"].strip():
                die("%s.read must say what visibly changes here" % where)
    elif block == "attire":
        if key == "materials":
            token_list(value, where)
        else:
            token(value, where)
    elif block == "features":
        if not isinstance(value, dict) or set(value) != set(meta["feature_keys"]):
            die("%s must name exactly %s" % (where, meta["feature_keys"]))
        token(value["form"], where + ".form")
        prose(value["note"], where + ".note", v["prose_min_chars"])
    elif key == "avoid":
        token_list(value, where)
    else:
        prose(value, where, v["prose_min_chars"])


def check_resolved_appearance(body, traits, label, meta):
    """Cross-layer agreement, judged on a complete body rather than a sparse delta."""
    scale = traits["body_scale"]
    floor, ceiling = meta["body_scale_height_m"][scale]
    for sex in meta["sexes"]:
        b = body["frame"]["height_m"][sex]
        if b["min"] < floor or b["max"] > ceiling:
            die("%s is body_scale %r, whose height envelope is %g..%g m; its %s band is "
                "%g..%g m" % (label, scale, floor, ceiling, sex, b["min"], b["max"]))
    stages = body["life_stages"]
    order = meta["life_stages"]
    onsets = [stages[s]["onset_years"] for s in order]
    if onsets[0] != meta["validation"]["child_onset_years_exact"]:
        die("%s life_stages.child must begin at year 0" % label)
    for index in range(len(order) - 1):
        if onsets[index] >= onsets[index + 1]:
            die("%s life_stages.%s begins at %g and %s at %g; stages run in order"
                % (label, order[index], onsets[index], order[index + 1], onsets[index + 1]))
    growth = [stages[s]["height_fraction"] for s in order[:3]]
    if not growth[0] < growth[1] < growth[2]:
        die("%s does not grow from child to adult: %s" % (label, growth))
    if stages["adult"]["height_fraction"] != meta["validation"]["adult_height_fraction_exact"]:
        die("%s life_stages.adult.height_fraction is the reference stature every band is "
            "stated at, and must be exactly 1" % label)
    if stages["max_years"] <= onsets[-1]:
        die("%s life_stages.max_years %g is not past its elder onset %g"
            % (label, stages["max_years"], onsets[-1]))
    low, high = meta["lifespan_tempo_years"][traits["lifespan_tempo"]]
    if not low <= stages["max_years"] <= high:
        die("%s has lifespan_tempo %r, whose span is %g..%g years; max_years is %g"
            % (label, traits["lifespan_tempo"], low, high, stages["max_years"]))


def lint_appearance(appearance, traits, meta):
    blocks = meta["appearance_blocks"]
    for field in ("races", "peoples"):
        mine, theirs = set(appearance[field]), set(traits[field])
        if mine != theirs:
            die("appearance must name exactly the %s traits names; check %s"
                % (field, sorted(mine ^ theirs)))
    for race, base in appearance["races"].items():
        label = "race %r" % race
        if set(base) != set(blocks):
            die("%s must name exactly the appearance blocks %s" % (label, sorted(blocks)))
        for block, keys in blocks.items():
            if set(base[block]) != set(keys):
                die("%s block %r must name exactly %s" % (label, block, sorted(keys)))
            for key, value in base[block].items():
                check_appearance_key(block, key, value, label, meta)
        check_resolved_appearance(base, traits["races"][race], label, meta)
    for cid, delta in appearance["peoples"].items():
        label = "people %r" % cid
        for block, keys in delta.items():
            if block not in blocks:
                die("%s extends unknown appearance block %r" % (label, block))
            if not isinstance(keys, dict):
                die("%s block %r must map keys to extensions" % (label, block))
            for key, value in keys.items():
                if key not in blocks[block]:
                    die("%s extends unknown key %s.%s" % (label, block, key))
                # A delta REPLACES a key whole, so its value must be complete on its own.
                check_appearance_key(block, key, value, label, meta)


# ---------------------------------------------------------------------------
# Appearance flattening
# ---------------------------------------------------------------------------

def flatten_value(value, nodes, kids, keys):
    """Append `value`'s subtree to the arena; return this node's index.

    Children are recorded as INDICES in a shared pool rather than as a contiguous run
    of nodes: a run is correct only while no child has children of its own, and an
    appearance body nests four deep.
    """
    index = len(nodes)
    nodes.append(None)  # reserve, so children take higher indices
    if isinstance(value, bool):
        nodes[index] = dict(kind="Boolean", boolean=value, span=(0, 0))
    elif isinstance(value, str):
        nodes[index] = dict(kind="Text", text=value, span=(0, 0))
    elif isinstance(value, int):
        # Kept apart from a float on purpose: the digest is over text, so an authored
        # `350` and an authored `350.0` are the same quantity and different bytes.
        nodes[index] = dict(kind="Integer", integer=value, span=(0, 0))
    elif is_number(value):
        nodes[index] = dict(kind="Number", number=value, span=(0, 0))
    elif isinstance(value, list):
        children = [flatten_value(item, nodes, kids, keys) for item in value]
        begin = len(kids)
        kids.extend(children)
        keys.extend([""] * len(children))
        nodes[index] = dict(kind="Array", span=(begin, len(kids)))
    elif isinstance(value, dict):
        children = [(key, flatten_value(item, nodes, kids, keys)) for key, item in value.items()]
        begin = len(kids)
        for key, child in children:
            kids.append(child)
            keys.append(key)
        nodes[index] = dict(kind="Object", span=(begin, len(kids)))
    else:
        die("appearance carries a value of a kind JSON should not produce: %r" % (value,))
    return index


def node_literal(node):
    return ("{Value::Kind::%s, %s, %s, %s, %dLL, %s, %du, %du}"
            % (node["kind"],
               cpp_string(node.get("text", "")),
               cpp_double(node["number"]) if "number" in node else "0.0",
               cpp_string(canonical_number(node["number"])) if "number" in node else '""',
               node.get("integer", 0),
               "true" if node.get("boolean") else "false",
               node["span"][0], node["span"][1]))


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

def load_all():
    meta = load("heritage-axes.json")
    documents = {
        "axes": meta,
        "phonology": load("heritage-phonology.json"),
        "name_draw": load("heritage-name-draw.json"),
        "traits": load("heritage-traits.json"),
        "derivation": load("heritage-derivation.json"),
        "lexicon": load("heritage-lexicon.json"),
        "overrides": load("heritage-overrides.json"),
        "appearance": load("heritage-appearance.json"),
    }
    if meta["policy_files"] != list(POLICY_FILES):
        die("the policy file order decides the published revision map; got %s"
            % meta["policy_files"])
    if len(meta["axes"]) != 17 or meta["craft_count"] != 2:
        die("the port's cardinalities are compiled in: 17 axes and 2 crafts")
    if meta["list_axes"] != ["craft_focus"]:
        die("craft_focus is the only list-valued axis the port has a field for")

    lint_traits(documents["traits"], meta)
    lint_derivation(documents["derivation"], meta)
    lint_lexicon(documents["lexicon"], documents["traits"], documents["phonology"], meta)
    lint_overrides(documents["overrides"], documents["traits"], meta)
    lint_appearance(documents["appearance"], documents["traits"], meta)
    return documents


def render_heritage():
    doc = load_all()
    meta = doc["axes"]
    phonology = doc["phonology"]
    axis_names = list(meta["axes"])
    axis_at = {name: i for i, name in enumerate(axis_names)}

    pool = Arrays()
    lines = [BANNER.format(source=", ".join(SOURCES))]
    lines.append('#include "fwg/heritage.hpp"\n')
    lines.append("#include <stdexcept>\n")
    lines.append("namespace fwg::heritage {\nnamespace {\n")

    body = []

    # --- axes and closed vocabularies ---
    axis_rows = []
    for name in axis_names:
        values = meta["crafts"] if name == "craft_focus" else meta["axes"][name]
        axis_rows.append("    {%s, %s, %s},"
                         % (cpp_string(name), pool.strings(values),
                            "true" if name in meta["list_axes"] else "false"))
    body.append("// The seventeen axes in declared order. A value's POSITION in its enum is how\n"
                "// every derivation map is indexed, which is what makes totality structural.")
    body.append("constexpr Axis kAxes[] = {")
    body.extend(axis_rows)
    body.append("};\n")

    body.append("constexpr std::string_view kCrafts[] = {%s};"
                % ", ".join(cpp_string(c) for c in meta["crafts"]))
    body.append("constexpr std::string_view kKnownSchools[] = {%s};"
                % ", ".join(cpp_string(s) for s in meta["known_schools"]))
    body.append("constexpr std::string_view kHiddenSchools[] = {%s};\n"
                % ", ".join(cpp_string(s) for s in meta["hidden_schools"]))

    body.append("struct EnvelopeRow { std::string_view name; double low; double high; };")
    body.append("constexpr EnvelopeRow kBodyScaleHeightM[] = {")
    for name, (low, high) in meta["body_scale_height_m"].items():
        body.append("    {%s, %s, %s}," % (cpp_string(name), cpp_double(low), cpp_double(high)))
    body.append("};")
    body.append("constexpr EnvelopeRow kLifespanTempoYears[] = {")
    for name, (low, high) in meta["lifespan_tempo_years"].items():
        body.append("    {%s, %s, %s}," % (cpp_string(name), cpp_double(low), cpp_double(high)))
    body.append("};\n")

    culture_keys = [(block, key)
                    for block, keys in doc["derivation"]["culture"].items()
                    for key in keys]
    if len(culture_keys) != 23:
        die("the port compiles in 23 culture keys; the derivation declares %d" % len(culture_keys))
    body.append("// In derivation-document order: this is the emission order of a culture block\n"
                "// and the position an override names.")
    body.append("constexpr CultureKey kCultureKeys[] = {")
    for block, key in culture_keys:
        body.append("    {%s, %s}," % (cpp_string(block), cpp_string(key)))
    body.append("};\n")

    # --- phonology ---
    body.append("// ORDER IS A CONTRACT. inventory() re-emits by filtering THESE tuples, the\n"
                "// result is then indexed and sliced positionally by every name draw, and it is\n"
                "// hashed into the heritage identity.")
    body.append("constexpr std::string_view kConsonants[] = {%s};"
                % ", ".join(cpp_string(c) for c in phonology["alphabet"]["consonants"]))
    body.append("constexpr std::string_view kVowels[] = {%s};"
                % ", ".join(cpp_string(v) for v in phonology["alphabet"]["vowels"]))
    body.append("constexpr std::string_view kDigraphs[] = {%s};\n"
                % ", ".join(cpp_string(d) for d in phonology["digraphs"]))

    body.append("constexpr std::string_view kCoreConsonants[] = {%s};"
                % ", ".join(cpp_string(c) for c in phonology["core_consonants"]))
    body.append("constexpr std::string_view kCoreVowels[] = {%s};\n"
                % ", ".join(cpp_string(v) for v in phonology["core_vowels"]))

    body.append("constexpr tables::Gate kGates[] = {")
    for axis in phonology["gated_axes"]:
        for value, rule in phonology["gates"][axis].items():
            body.append("    {%d, %s, %s, %s},"
                        % (axis_at[axis], cpp_string(value),
                           pool.strings(rule.get("add", [])), pool.strings(rule.get("drop", []))))
    body.append("};\n")

    clusters = phonology["cluster_tables"]
    body.append("constexpr std::array<std::string_view, 2> kRefusedOnsets[] = {%s};"
                % ", ".join("{%s, %s}" % (cpp_string(a), cpp_string(b))
                            for a, b in clusters["refused_stop_liquid_onsets"]))
    body.append("constexpr tables::Homorganic kHomorganic[] = {%s};\n"
                % ", ".join("{%s, %s}" % (cpp_string(nasal), pool.strings(stops))
                            for nasal, stops in clusters["homorganic_nasal_stop_codas"]))

    body.append("constexpr std::string_view kWordOnsetExcludes[] = {%s};\n"
                % ", ".join(cpp_string(t) for t in phonology["word_onset_excludes"]))

    body.append("constexpr tables::PatronymicRow kPatronymic[] = {")
    for pattern, value in phonology["patronymic_by_naming_pattern"].items():
        body.append("    {%s, %s}," % (cpp_string(pattern), cpp_string(value)))
    body.append("};\n")

    grammar = phonology["syllable_template_grammar"]
    if grammar["split_on"] != "V" or grammar["onset_token"] != "(C)" \
            or grammar["coda_tokens"] != ["(C)", "(N)"]:
        die("the template shape parser is compiled in as split-on-V and literal (C)/(N) counts")

    # --- traits ---
    def trait_rows(name, rows):
        out = ["constexpr tables::TraitRow %s[] = {" % name]
        for cid, record in rows.items():
            entries = []
            for axis, value in record.items():
                if axis == "craft_focus":
                    entries.append('{%d, "", {%s, %s}}'
                                   % (axis_at[axis], cpp_string(value[0]), cpp_string(value[1])))
                else:
                    entries.append("{%d, %s, {}}" % (axis_at[axis], cpp_string(value)))
            pool_name = pool.literals("tables::AxisValue", entries)
            out.append("    {%s, %s}," % (cpp_string(cid), pool_name))
        out.append("};\n")
        return out

    body.extend(trait_rows("kTraitRaces", doc["traits"]["races"]))
    body.extend(trait_rows("kTraitPeoples", doc["traits"]["peoples"]))

    # --- derivation ---
    def rule_literal(section, block, key, rule):
        path = "%s.%s.%s" % (section, block, key)
        if "copy" in rule:
            return "{tables::Rule::Kind::Copy, %d, -1, {}}" % axis_at[rule["copy"]]
        if "craft" in rule:
            return "{tables::Rule::Kind::Craft, -1, %d, {}}" % rule["craft"]
        axis = rule["from"]
        ordered = [rule["map"][v] for v in meta["axes"][axis]]
        if any(not isinstance(v, str) for v in ordered):
            die("%s maps to something other than a string" % path)
        return ("{tables::Rule::Kind::Map, %d, -1, %s}"
                % (axis_at[axis], pool.strings(ordered)))

    body.append("// Parallel to kCultureKeys.")
    body.append("constexpr tables::Rule kCultureRules[] = {")
    for block, keys in doc["derivation"]["culture"].items():
        for key, rule in keys.items():
            body.append("    %s," % rule_literal("culture", block, key, rule))
    body.append("};\n")

    def typed_rule(block, key, kind):
        rule = doc["derivation"]["genome"][block][key]
        axis = rule["from"]
        ordered = [rule["map"][v] for v in meta["axes"][axis]]
        if kind == "text":
            if any(not isinstance(v, str) for v in ordered):
                die("genome %s.%s must map to strings" % (block, key))
            return "{%d, %s}" % (axis_at[axis], pool.strings(ordered))
        if kind == "bool":
            if any(not isinstance(v, bool) for v in ordered):
                die("genome %s.%s must map to booleans" % (block, key))
            return "{%d, %s}" % (axis_at[axis], pool.bools(ordered))
        if any(isinstance(v, bool) or not is_number(v) for v in ordered):
            die("genome %s.%s must map to numbers" % (block, key))
        return "{%d, %s}" % (axis_at[axis], pool.numbers(ordered))

    genome_fields = [
        ("phonotactics", "syllable", "text"), ("phonotactics", "vowel_length", "text"),
        ("phonotactics", "hiatus", "text"), ("phonotactics", "geminates", "bool"),
        ("prosody", "stress", "text"), ("prosody", "tone", "text"),
        ("prosody", "word_shape", "text"),
        ("morphology", "compounding", "text"), ("morphology", "honorific", "text"),
        ("morphology", "call_form", "bool"),
        ("orthography", "script_form", "text"), ("orthography", "direction", "text"),
        ("rates", "third_syllable", "number"), ("rates", "cluster", "number"),
        ("rates", "loanword", "number"),
    ]
    authored = {(block, key) for block, keys in doc["derivation"]["genome"].items()
                for key in keys}
    if authored != {(block, key) for block, key, _ in genome_fields}:
        die("the genome derivation has grown or lost a key; every one lands in a typed "
            "field, so add the field before the rule. Difference: %s"
            % sorted(authored ^ {(b, k) for b, k, _ in genome_fields}))
    body.append("constexpr tables::GenomeDerivation kGenomeDerivation = {")
    for block, key, kind in genome_fields:
        body.append("    %s,  // %s.%s" % (typed_rule(block, key, kind), block, key))
    body.append("};\n")

    # --- lexicon ---
    lex = doc["lexicon"]
    family_rows = []
    for name, entry in lex["families"].items():
        branch_entries = []
        for branch, spec in entry["branches"].items():
            changes = pool.literals("SoundChange",
                                 ["{%s, %s}" % (cpp_string(a), cpp_string(b))
                                  for a, b in spec.get("sound_changes", [])])
            branch_entries.append("{%s, %s, %s, %s}"
                                  % (cpp_string(branch), changes,
                                     pool.morphemes(list(spec.get("add_roots", {}).items())),
                                     pool.strings(spec.get("drop_roots", []))))
        branches = pool.literals("BranchSpec", branch_entries)
        family_rows.append("    {%s, %s, %s, %s, %s, %s},"
                           % (cpp_string(name), cpp_string(entry["parent_race"]),
                              cpp_string(entry["word_order"]), cpp_string(entry["affixing"]),
                              pool.morphemes(list(entry["roots"].items())), branches))
    body.append("// Family roots keep their AUTHORED ORDER: branch_lexicon walks them in it.")
    body.append("constexpr LexiconFamily kFamilies[] = {")
    body.extend(family_rows)
    body.append("};\n")

    body.append("constexpr std::string_view kSharedSlots[] = {%s};"
                % ", ".join(cpp_string(s) for s in lex["shared_slots"]))
    body.append("// Copied verbatim into every genome. Reordering it renames nothing and moves\n"
                "// the heritage digest, because sorted keys do not sort list elements.")
    body.append("constexpr std::string_view kForbiddenForms[] = {%s};\n"
                % ", ".join(cpp_string(f) for f in lex["forbidden_forms"]))

    for kind, symbol in (("personal_templates", "kPersonalTemplates"),
                         ("settlement_templates", "kSettlementTemplates")):
        body.append("// ORDER IS A CONTRACT: a cumulative weighted walk in THIS order decides\n"
                    "// which template a draw lands on.")
        body.append("constexpr NameTemplate %s[] = {" % symbol)
        for template in lex[kind]:
            body.append("    {{%s, %s}, %d},"
                        % (cpp_string(template["pattern"][0]),
                           cpp_string(template["pattern"][1]), template["weight"]))
        body.append("};\n")

    body.append("constexpr SlotRole kSlotRoles[] = {")
    for role, slots in lex["slot_roles"].items():
        body.append("    {%s, %s}," % (cpp_string(role), pool.strings(slots)))
    body.append("};\n")

    # --- overrides ---
    culture_at = {(block, key): i for i, (block, key) in enumerate(culture_keys)}
    body.append("constexpr tables::CultureOverrideRow kCultureOverrides[] = {")
    for cid, patch in doc["overrides"].get("culture", {}).items():
        entries = []
        for block, keys in patch.items():
            for key, value in keys.items():
                entries.append("{%d, %s}" % (culture_at[(block, key)], cpp_string(value)))
        body.append("    {%s, %s},"
                    % (cpp_string(cid), pool.literals("tables::CulturePatch", entries)))
    body.append("};\n")

    body.append("constexpr tables::GenomeOverrideRow kGenomeOverrides[] = {")
    for cid, patch in doc["overrides"].get("genome", {}).items():
        entries = []
        for block, keys in patch.items():
            for key, value in keys.items():
                head = "tables::GenomePatch::Kind::%s, %s, %s"
                if isinstance(value, dict):
                    entries.append('{%s, "", false, {}, %s, %s}'
                                   % (head % ("Inventory", cpp_string(block), cpp_string(key)),
                                      pool.strings(value.get("add", [])),
                                      pool.strings(value.get("drop", []))))
                elif isinstance(value, bool):
                    entries.append('{%s, "", %s, {}, {}, {}}'
                                   % (head % ("Boolean", cpp_string(block), cpp_string(key)),
                                      "true" if value else "false"))
                elif is_number(value):
                    entries.append('{%s, "", false, {%s, %s}, {}, {}}'
                                   % (head % ("Number", cpp_string(block), cpp_string(key)),
                                      cpp_double(value), cpp_string(canonical_number(value))))
                else:
                    entries.append("{%s, %s, false, {}, {}, {}}"
                                   % (head % ("Text", cpp_string(block), cpp_string(key)),
                                      cpp_string(value)))
        body.append("    {%s, %s},"
                    % (cpp_string(cid), pool.literals("tables::GenomePatch", entries)))
    body.append("};\n")

    # --- name draw ---
    nd = doc["name_draw"]
    curve, rates, shapes = nd["stock_curve"], nd["draw_rates"], nd["word_shape_syllables"]
    if rates["stock_draws_per_name"] != 2:
        die("stock_name spends exactly two draws on every path; that is compiled in")
    if len(curve["head_shares"]) != 5 or curve["head_spread"] != 5:
        die("the head table and the head spread are both five; the port compiles that in")
    body.append("constexpr tables::NameDraw kNameDraw = {")
    body.append("    %s, %s, %s, %d, %d,"
                % (pool.doubles(curve["head_shares"]), cpp_double(curve["zipf_exponent"]),
                   cpp_double(curve["rare_share"]), curve["head_spread"], nd["redraw_limit"]))
    body.append("    %s, %s, %s, %s,"
                % (cpp_double(rates["nucleus_complex_share"]["base"]),
                   cpp_double(rates["nucleus_complex_share"]["vowel_length_contrastive"]),
                   cpp_double(rates["nucleus_complex_share"]["hiatus_preferred"]),
                   cpp_double(rates["optional_coda_skip"])))
    body.append("    %d, %d, %d, %s, %d, %d,"
                % (shapes["monosyllabic_heavy"], shapes["disyllabic"],
                   shapes["trisyllabic_flowing"], cpp_string("".join(nd["vowel_letters"])),
                   nd["name_table_slices"]["consonants"], nd["name_table_slices"]["vowels"]))
    body.append("};\n")

    # --- appearance ---
    nodes, kids, keys = [], [], []
    race_roots = [(cid, flatten_value(value, nodes, kids, keys))
                  for cid, value in doc["appearance"]["races"].items()]
    people_roots = [(cid, flatten_value(value, nodes, kids, keys))
                    for cid, value in doc["appearance"]["peoples"].items()]
    for index, node in enumerate(nodes):
        begin, end = node["span"]
        if not (0 <= begin <= end <= len(kids)):
            die("appearance node %d has a child span outside the pool" % index)
    for child in kids:
        if not 0 <= child < len(nodes):
            die("an appearance child index points outside the node arena")

    body.append("// A body is a tree, so it is stored the way the habitat predicates are: one\n"
                "// node arena, a shared pool of child INDICES, and the key each child hangs\n"
                "// under. Prose is the deliverable here -- these bytes feed the art pipeline\n"
                "// and the identity digest, so they are copied and never reflowed.")
    body.append("constexpr tables::ValueNode kAppearanceNodes[] = {")
    for node in nodes:
        body.append("    %s," % node_literal(node))
    body.append("};")
    body.append("constexpr std::uint32_t kAppearanceChildren[] = {%s};"
                % ", ".join("%du" % c for c in kids))
    body.append("constexpr std::string_view kAppearanceChildKeys[] = {%s};\n"
                % ", ".join(cpp_string(k) for k in keys))

    body.append("constexpr tables::BodyRow kAppearanceRaces[] = {%s};"
                % ", ".join("{%s, %du}" % (cpp_string(cid), root) for cid, root in race_roots))
    body.append("constexpr tables::BodyRow kAppearancePeoples[] = {%s};\n"
                % ", ".join("{%s, %du}" % (cpp_string(cid), root) for cid, root in people_roots))

    body.append("constexpr std::string_view kSexes[] = {%s};"
                % ", ".join(cpp_string(s) for s in meta["sexes"]))
    body.append("constexpr std::string_view kLifeStages[] = {%s};\n"
                % ", ".join(cpp_string(s) for s in meta["life_stages"]))

    # Pooled last of all, so the accessor below sees the same array names as the tables.
    cluster_spans = tuple(pool.strings(clusters[key])
                          for key in ("onset_stops", "voiceless_stops", "liquids", "nasals"))

    # The pooled arrays have to be declared before the tables that point at them.
    lines.extend(pool.lines)
    lines.append("")
    lines.extend(body)
    lines.append("}  // namespace\n")

    # --- accessors ---
    lines.append("std::span<const Axis> axes() { return kAxes; }\n")
    lines.append("""int axis_index(std::string_view name) {
    int index = 0;
    for (const Axis& axis : kAxes) {
        if (axis.name == name) return index;
        ++index;
    }
    return -1;
}
""")
    lines.append("std::span<const std::string_view> crafts() { return kCrafts; }")
    lines.append("std::span<const std::string_view> known_schools() { return kKnownSchools; }")
    lines.append("std::span<const std::string_view> hidden_schools() { return kHiddenSchools; }\n")
    lines.append("""namespace {
std::pair<double, double> envelope(std::span<const EnvelopeRow> rows, std::string_view name,
                                   std::string_view what) {
    for (const EnvelopeRow& row : rows) {
        if (row.name == name) return {row.low, row.high};
    }
    throw std::invalid_argument(std::string("no ") + std::string(what) + " envelope for '" +
                                std::string(name) + "'");
}
}  // namespace

std::pair<double, double> body_scale_height_m(std::string_view body_scale) {
    return envelope(kBodyScaleHeightM, body_scale, "body_scale");
}

std::pair<double, double> lifespan_tempo_years(std::string_view lifespan_tempo) {
    return envelope(kLifespanTempoYears, lifespan_tempo, "lifespan_tempo");
}
""")
    lines.append("std::span<const CultureKey> culture_keys() { return kCultureKeys; }\n")
    lines.append("Alphabet alphabet() { return {kConsonants, kVowels}; }\n")
    lines.append("""bool is_digraph(std::string_view token) {
    for (std::string_view d : kDigraphs) {
        if (d == token) return true;
    }
    return false;
}
""")
    lines.append("Revisions revisions() { return {%s}; }\n"
                 % ", ".join(str(doc[name]["source_identity"]["revision"])
                             for name in POLICY_FILES))

    lines.append("""const Lexicon& lexicon() {
    static const Lexicon document{kSharedSlots,       kFamilies,
                                  kForbiddenForms,    kPersonalTemplates,
                                  kSettlementTemplates, kSlotRoles};
    return document;
}
""")

    lines.append("namespace tables {\n")
    for signature, value in (
            ("std::span<const TraitRow> trait_races()", "kTraitRaces"),
            ("std::span<const TraitRow> trait_peoples()", "kTraitPeoples"),
            ("std::span<const Rule> culture_rules()", "kCultureRules"),
            ("std::span<const Gate> gates()", "kGates"),
            ("std::span<const std::string_view> core_consonants()", "kCoreConsonants"),
            ("std::span<const std::string_view> core_vowels()", "kCoreVowels"),
            ("std::span<const std::string_view> word_onset_excludes()", "kWordOnsetExcludes"),
            ("std::span<const PatronymicRow> patronymic_table()", "kPatronymic"),
            ("std::span<const CultureOverrideRow> culture_overrides()", "kCultureOverrides"),
            ("std::span<const GenomeOverrideRow> genome_overrides()", "kGenomeOverrides"),
            ("std::span<const ValueNode> appearance_nodes()", "kAppearanceNodes"),
            ("std::span<const std::uint32_t> appearance_children()", "kAppearanceChildren"),
            ("std::span<const std::string_view> appearance_child_keys()", "kAppearanceChildKeys"),
            ("std::span<const BodyRow> appearance_races()", "kAppearanceRaces"),
            ("std::span<const BodyRow> appearance_peoples()", "kAppearancePeoples"),
            ("std::span<const std::string_view> sexes()", "kSexes"),
            ("std::span<const std::string_view> life_stage_order()", "kLifeStages")):
        lines.append("%s { return %s; }" % (signature, value))
    lines.append("")
    lines.append("const GenomeDerivation& genome_derivation() { return kGenomeDerivation; }")
    lines.append("""const ClusterTables& cluster_tables() {
    static const ClusterTables t{%s, %s, %s, %s, kRefusedOnsets, kHomorganic};
    return t;
}""" % cluster_spans)
    lines.append("const NameDraw& name_draw() { return kNameDraw; }\n")
    lines.append("}  // namespace tables\n")
    lines.append("}  // namespace fwg::heritage")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_heritage.cpp": render_heritage}
