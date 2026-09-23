"""The creature catalogue: 680 habitat profiles and 21 feeding roles.

This is the largest authored table in the library, so it is rendered as flat constexpr
arrays with spans into them rather than as a row of little objects: one array of weight
terms, one of requirement terms, one of biome tables, and one row per profile holding
spans. Nothing allocates and nothing is parsed at runtime.

Two orderings reach generated worlds and are preserved here deliberately:

  * PROFILE ORDER. _place builds a per-cell candidate list in catalogue order, float-sums
    the rates over it and walks it cumulatively, so reordering the same rows is a
    different world on the same seed. The table is stored grouped by class -- animals,
    then monsters -- with file order kept inside each group, because the two passes only
    ever see the filtered sequences and those are unchanged by the grouping.
  * TERM ORDER inside a profile's `weights`. The score is a float sum over the terms, so
    the JSON object's key order is load-bearing and a dict that reorders keys would move
    sites. Terms are emitted in file order, never sorted.

Validation mirrors the Python loader's and is fail-closed for the same reason: a
catalogue that has lost a row or a table is not a smaller catalogue, it is a broken one.
"""
from embed.common import BANNER, OUT, cpp_double, cpp_string, load

SOURCE = "nest-profiles.json"

MEDIA = ("land", "shore", "marine", "freshwater")
CLASSES = ("animal", "monster")

# Biome ids run 0..17 with gaps. Must match kNestBiomeCount in include/fwg/nests.hpp;
# a table is a dense array over that range so the lookup is an index, not a search.
BIOME_COUNT = 18

# The ley schools generation can never raise. A `requires` on one is a hard gate against
# a layer that is identically zero in every world the generator can build, which is the
# whole of what is decidable about a profile without generating worlds.
DORMANT_FIELDS = frozenset(("ley_blood", "ley_void", "ley_rot", "ley_eldritch"))

MEDIUM_ENUM = {"land": "Land", "shore": "Shore", "marine": "Marine", "freshwater": "Freshwater"}
CLASS_ENUM = {"animal": "Animal", "monster": "Monster"}


def load_nest_profiles():
    doc = load(SOURCE, 1)
    rows = doc["profiles"]
    roles = doc["roles"]

    ids = [p["id"] for p in rows]
    if len(set(ids)) != len(ids):
        raise SystemExit("nest-profiles.json has a duplicate creature profile id")
    if not rows:
        raise SystemExit("nest-profiles.json has no creature profiles")
    if not roles:
        raise SystemExit("nest-profiles.json has no feeding roles; no animal could resolve a biome table")

    for name, role in roles.items():
        table = role.get("biome_weights")
        if not table:
            raise SystemExit("feeding role %s has no biome_weights; every animal that "
                             "claims it would be equally at home on a glacier and in a "
                             "rainforest" % name)
        check_biome_table(table, "role " + name)

    for p in rows:
        if p["medium"] not in MEDIA:
            raise SystemExit("Invalid nest profile (medium): " + p["name"])
        if not p["weights"] or any(v <= 0 for v in p["weights"].values()):
            raise SystemExit("Invalid nest profile (weights): " + p["name"])
        if p["class"] not in CLASSES or not 1 <= p["tier"] <= 5:
            raise SystemExit("Creature needs class animal or monster and tier 1..5: " + p["name"])
        if p["class"] == "animal" and p.get("role") not in roles:
            raise SystemExit("Animal needs a known role: " + p["name"])
        # A monster has no feeding role, so a monster without its own table would fall
        # through to "no preference" and be equally at home everywhere. That fallback was
        # the whole of the monster half's opinion about biome until it was ruled out;
        # refuse it rather than let it come back.
        if p["class"] == "monster" and not p.get("biome_weights"):
            raise SystemExit("Monster needs its own biome_weights: " + p["name"])
        if p["class"] == "animal" and p.get("biome_weights"):
            raise SystemExit("An animal resolves its biome table through its feeding role; "
                             "a table of its own would silently win over the role and no "
                             "loader reads it: " + p["name"])
        if p.get("biome_weights"):
            check_biome_table(p["biome_weights"], p["id"])
        if len(p["temperature"]) != 2 or p["temperature"][0] > p["temperature"][1]:
            raise SystemExit("temperature is an inclusive [min, max] band: " + p["name"])
        if p["spacing_m"] <= 0:
            raise SystemExit("spacing_m is a positive territory diameter: " + p["name"])
        if p["occurrence"] <= 0:
            raise SystemExit("occurrence is a positive share of its tier: " + p["name"])
        # `real` is asset identity on the row, and a placed site recomputes it as
        # (class == animal). The generated table therefore does not store it -- which is
        # only sound while the two agree, so the moment they stop, this refuses.
        if p["real"] != (p["class"] == "animal"):
            raise SystemExit("`real` must equal (class == animal): " + p["name"])
        # Read by later stages rather than by a nest pass, and carried only because the
        # block publishes the catalogue. A row that lost one would reach those stages as
        # an empty string instead of as an error.
        if not p.get("movement"):
            raise SystemExit("every creature needs a movement mode: " + p["name"])
        if not isinstance(p.get("sky"), bool):
            raise SystemExit("`sky` is a flag: " + p["name"])
        if p["class"] == "monster" and not p.get("likeness"):
            raise SystemExit("a monster needs an art reference: " + p["name"])
        if p["class"] == "animal" and (p.get("likeness") or p.get("likeness_traits")):
            raise SystemExit("an animal IS its own likeness and carries no reference: " + p["name"])

    return rows, roles


def check_biome_table(table, owner):
    for key, value in table.items():
        biome = int(key)
        if not 0 <= biome < BIOME_COUNT:
            raise SystemExit("%s names biome %d, outside the 0..%d the tables span"
                             % (owner, biome, BIOME_COUNT - 1))
        # An absent key is exclusion and a zero would say the same thing less clearly;
        # a negative would make `weight > 0` refuse ground the author meant to grant.
        if not 0 < value <= 1:
            raise SystemExit("%s weights biome %d at %r, outside (0, 1]" % (owner, biome, value))


def field_names(rows):
    """Every habitat field any profile scores on or requires, sorted.

    These are habitat FIELDS, not biomes: some name layers and seven are derived per
    cell by the pass itself. The sort is only for a stable file; nothing about the order
    reaches a world, because a term carries its own field id.
    """
    names = set()
    for p in rows:
        names.update(p["weights"])
        names.update(p["requires"])
    return sorted(names)


def biome_table_literal(table):
    values = [0.0] * BIOME_COUNT
    for key, value in table.items():
        values[int(key)] = float(value)
    return ", ".join(cpp_double(v) for v in values)


def render_nests():
    rows, roles = load_nest_profiles()
    names = field_names(rows)
    field_id = {name: i for i, name in enumerate(names)}

    # Animals first, then monsters, file order preserved inside each group.
    animals = [p for p in rows if p["class"] == "animal"]
    monsters = [p for p in rows if p["class"] == "monster"]
    ordered = animals + monsters

    # Biome tables: the 21 roles first, then one per monster. An animal has no table of
    # its own -- it resolves through its role -- so it points straight at its role's
    # block and the indirection disappears before the inner loop ever runs.
    role_names = list(roles)
    biome_blocks = []
    role_offset = {}
    for name in role_names:
        role_offset[name] = len(biome_blocks) * BIOME_COUNT
        biome_blocks.append((name, roles[name]["biome_weights"]))
    profile_biome_offset = {}
    for p in ordered:
        if p["class"] == "monster":
            profile_biome_offset[p["id"]] = len(biome_blocks) * BIOME_COUNT
            biome_blocks.append((p["id"], p["biome_weights"]))
        else:
            profile_biome_offset[p["id"]] = role_offset[p["role"]]

    weights = []
    requirements = []
    traits = []
    spans = {}
    trait_spans = {}
    for p in ordered:
        w_begin = len(weights)
        for key, value in p["weights"].items():
            weights.append((field_id[key], float(value)))
        r_begin = len(requirements)
        for key, value in p["requires"].items():
            requirements.append((field_id[key], float(value)))
        spans[p["id"]] = (w_begin, len(weights) - w_begin, r_begin, len(requirements) - r_begin)
        t_begin = len(traits)
        traits.extend(p.get("likeness_traits", []))
        trait_spans[p["id"]] = (t_begin, len(traits) - t_begin)

    lines = [BANNER.format(source=SOURCE)]
    lines.append('#include "fwg/nests.hpp"\n')
    lines.append("namespace fwg {")
    lines.append("namespace {\n")

    lines.append("// Habitat field names, interned. Seven of them -- depth, dryness, coast, mountain,")
    lines.append("// plains, wetland, cold -- are derived per cell by the pass; the rest name layers,")
    lines.append("// and a layer this world does not carry reads 0.0, so a `requires` on it fails")
    lines.append("// closed and a `weights` term on it contributes nothing.")
    lines.append("constexpr std::string_view kFieldNames[] = {")
    for name in names:
        lines.append("    %s," % cpp_string(name))
    lines.append("};\n")

    lines.append("// (field id, weight). FILE ORDER: the score is a float sum over these terms.")
    lines.append("constexpr HabitatTerm kWeights[] = {")
    lines.extend(chunked("    ", ["{%d, %s}," % (f, cpp_double(v)) for f, v in weights]))
    lines.append("};\n")

    lines.append("// (field id, hard minimum on the RAW field value -- `depth: 30` is 30 metres).")
    lines.append("constexpr HabitatTerm kRequires[] = {")
    lines.extend(chunked("    ", ["{%d, %s}," % (f, cpp_double(v)) for f, v in requirements]))
    lines.append("};\n")

    lines.append("// Biome tables, %d doubles each, indexed by biome id. Zero is EXCLUSION, not" % BIOME_COUNT)
    lines.append("// neutrality: the authored tables are sparse and an absent biome is ground the")
    lines.append("// species does not use. The %d role tables come first and every animal points at" % len(role_names))
    lines.append("// its role's block, so no row resolves a table at runtime.")
    lines.append("constexpr double kBiomeWeights[] = {")
    for owner, table in biome_blocks:
        lines.append("    %s,  // %s" % (biome_table_literal(table), owner))
    lines.append("};\n")

    lines.append("// Art-reference traits, flattened. Nothing in a nest pass reads these; the block")
    lines.append("// publishes the catalogue and beast movement, encounters and the asset list read")
    lines.append("// them from there, so dropping them here would delete data three later stages need.")
    lines.append("constexpr std::string_view kLikenessTraits[] = {")
    lines.extend(chunked("    ", ["%s," % cpp_string(t) for t in traits], per_line=5))
    lines.append("};\n")

    lines.append("constexpr CreatureRole kRoles[] = {")
    for name in role_names:
        lines.append("    {%s, {kBiomeWeights + %d, %d}},"
                     % (cpp_string(name), role_offset[name], BIOME_COUNT))
    lines.append("};\n")

    lines.append("// ORDER IS A CONTRACT: the per-cell candidate list is built in this order and the")
    lines.append("// cumulative pick walks it, so reordering the same rows is a different world on")
    lines.append("// the same seed. Animals occupy [0, %d) and monsters [%d, %d)."
                 % (len(animals), len(animals), len(ordered)))
    lines.append("constexpr CreatureProfile kProfiles[] = {")
    for p in ordered:
        w_begin, w_count, r_begin, r_count = spans[p["id"]]
        total = sum(float(v) for v in p["weights"].values())
        dormant = any(k in DORMANT_FIELDS and v > 0 for k, v in p["requires"].items())
        t_begin, t_count = trait_spans[p["id"]]
        lines.append("    {%s, %s, CreatureClass::%s, %d, %s, %s, %s, %s, CreatureMedium::%s, "
                     "%s, %s, {kWeights + %d, %d}, {kRequires + %d, %d}, %s, "
                     "{kBiomeWeights + %d, %d}, %s, %s, %s, "
                     "%s, %s, %s, {kLikenessTraits + %d, %d}, %s},"
                     % (cpp_string(p["id"]), cpp_string(p["name"]),
                        CLASS_ENUM[p["class"]], p["tier"],
                        cpp_string(p.get("role") or ""), cpp_string(p["family"]),
                        cpp_string(p["size"]), cpp_string(p["kind"]),
                        MEDIUM_ENUM[p["medium"]],
                        cpp_double(p["temperature"][0]), cpp_double(p["temperature"][1]),
                        w_begin, w_count, r_begin, r_count, cpp_double(total),
                        profile_biome_offset[p["id"]], BIOME_COUNT,
                        cpp_double(p["spacing_m"]), cpp_double(p["occurrence"]),
                        "true" if dormant else "false",
                        cpp_string(p["movement"]), "true" if p["sky"] else "false",
                        cpp_string(p.get("likeness") or ""), t_begin, t_count,
                        cpp_string(p.get("likeness_secondary") or "")))
    lines.append("};\n")

    lines.append("constexpr std::size_t kAnimalCount = %d;" % len(animals))
    lines.append("constexpr std::size_t kMonsterCount = %d;\n" % len(monsters))
    lines.append("}  // namespace\n")

    lines.append("std::span<const CreatureProfile> creature_profiles() { return kProfiles; }\n")
    lines.append("std::span<const CreatureProfile> creature_catalogue(CreatureClass which) {")
    lines.append("    return which == CreatureClass::Animal")
    lines.append("               ? std::span<const CreatureProfile>(kProfiles, kAnimalCount)")
    lines.append("               : std::span<const CreatureProfile>(kProfiles + kAnimalCount, kMonsterCount);")
    lines.append("}\n")
    lines.append("std::span<const CreatureRole> creature_roles() { return kRoles; }\n")
    lines.append("std::span<const std::string_view> nest_field_names() { return kFieldNames; }\n")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def chunked(indent, items, per_line=6):
    """Long flat arrays, several entries to a line: 680 profiles make big tables."""
    out = []
    for i in range(0, len(items), per_line):
        out.append(indent + " ".join(items[i:i + per_line]))
    return out


TARGETS = {OUT / "content_nests.cpp": render_nests}
