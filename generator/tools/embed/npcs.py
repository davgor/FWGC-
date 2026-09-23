"""The NPC post policy: 88 staffing rosters, 84 verb rows, 39 giver ranks, 5 caps.

Two list orders are hard contracts here, both positional rather than probabilistic --
`npc_roster` consumes no sequential RNG stream at all, so iteration order never moves a
drawn value, and these are the only two orderings that reach a world:

  * `giver_rank` IS AN INDEX-INTO-LIST RANK, best first. The earmark sorts candidates on
    that index and truncates at the cap, so reordering the list changes which NPCs are
    important. The reference's own lint only checks the SET, which leaves the order
    completely unguarded; the duplicate and coverage checks below are that guard.
  * A building's role rows are SENIORITY order. The first role of the first row is the
    building's senior post and the only one the earmark can reach, the truncation repair
    cuts the TAIL, and the padding repair repeats the LAST role.

The inversion worth stating twice, because getting it backwards is silent: a building
ABSENT from `giver_rank` is not rank 0, it is "never a quest giver". Rank 0 is the BEST
rank. A port that defaults a missing rank to 0 inverts the meaning exactly, which is why
`npc_giver_index` returns -1 and the caller tests for it.

`buildings` and `role_verbs` are emitted sorted by key so both lookups are binary
searches; neither order reaches a world (the block re-sorts role_verbs before exporting
it, and buildings is only ever a lookup).

The names policy carries NO naming data. It is a handover record, and the only live thing
in it is a gate: `races` MUST BE ABSENT, because its presence would mean something had
reintroduced the parent-race model that gave four peoples human names outright. That gate
runs here, at generation time, rather than being dropped along with the empty document.
"""
from embed.common import BANNER, OUT, cpp_string, load

POSTS = "npc-posts.json"
NAMES = "npc-names-handover.json"
CODE_TABLES = "npc-code-tables.json"
APPEARANCE = "npc-appearance.json"
BODIES = "heritage-appearance.json"
AGE_BANDS = ("young", "middle", "old")

CLASS_CAPS = ("capital", "medium", "small")
SITE_KINDS = ("hamlet", "fortress")


def revision_of(doc, name):
    identity = doc.get("source_identity") or {}
    revision = identity.get("revision")
    if type(revision) is not int or revision < 1:
        raise SystemExit("%s needs a positive integer source_identity.revision" % name)
    return revision


def lint_posts(doc):
    verbs = doc.get("verbs")
    if not isinstance(verbs, list) or not verbs or len(set(verbs)) != len(verbs):
        raise SystemExit("posts policy needs a non-empty list of distinct verbs")
    known = set(verbs)
    for role, tagged in doc["role_verbs"].items():
        if not isinstance(tagged, list) or len(set(tagged)) != len(tagged):
            raise SystemExit("role %s needs a list of distinct verbs" % role)
        unknown = sorted(set(tagged) - known)
        if unknown:
            raise SystemExit("role %s offers verbs outside the canonical list: %s" % (role, unknown))
    buildings = doc["buildings"]
    if not buildings:
        raise SystemExit("posts policy lists no staffed buildings")
    for building, roles in buildings.items():
        if not roles:
            raise SystemExit("%s is listed with no roles; omit it instead" % building)
        seen = set()
        for row in roles:
            if len(row) != 2 or not isinstance(row[0], str) or type(row[1]) is not int or row[1] < 0:
                raise SystemExit("%s has a malformed role row %r" % (building, row))
            if row[0] in seen:
                raise SystemExit("%s repeats role %r" % (building, row[0]))
            seen.add(row[0])
        # Without a positive target the expansion is empty and the repair path would read
        # off the end of it. The reference relies on this lint for exactly that reason.
        if not any(target for _, target in roles):
            raise SystemExit("%s has no post with a positive target; omit it instead" % building)
    missing = sorted(set(doc["giver_rank"]) - set(buildings))
    if missing:
        raise SystemExit("giver_rank names buildings with no roster: %s" % missing)
    if len(set(doc["giver_rank"])) != len(doc["giver_rank"]):
        raise SystemExit("giver_rank repeats a building, so one of the two ranks is unreachable")
    caps = doc["caps"]
    for key in CLASS_CAPS + SITE_KINDS:
        if type(caps.get(key)) is not int or caps[key] < 0:
            raise SystemExit("caps needs a non-negative integer for %s" % key)
    # What a post offers the player beyond quest verbs (2026-09-23). A service on a role no
    # building staffs would be offered by nobody.
    staffed = {row[0] for roles in buildings.values() for row in roles}
    for role, services in doc.get("services", {}).items():
        if role not in staffed:
            raise SystemExit("services names %r, which no building staffs" % role)
        if (not isinstance(services, list) or not services or len(set(services)) != len(services)
                or not all(isinstance(s, str) and s for s in services)):
            raise SystemExit("role %s needs a non-empty list of distinct services" % role)
    return doc


def lint_weights(rows, label):
    if not isinstance(rows, list) or not rows:
        raise SystemExit("%s needs a non-empty list of [id, weight] rows" % label)
    for row in rows:
        if len(row) != 2 or not isinstance(row[0], str) or not row[0] or type(row[1]) is not int or row[1] < 1:
            raise SystemExit("%s has a malformed row %r; weights are positive integers" % (label, row))
    if len({row[0] for row in rows}) != len(rows):
        raise SystemExit("%s repeats an id" % label)


def lint_appearance(doc, bodies):
    lint_weights(doc.get("genders"), "genders")
    # A gender a game cannot find a frame for is a person it cannot draw.
    sexes = set()
    for race in bodies["races"].values():
        sexes |= set(race["frame"]["height_m"])
    unknown = sorted({row[0] for row in doc["genders"]} - sexes)
    if unknown:
        raise SystemExit("genders %s have no frame in %s, which states %s" % (unknown, BODIES, sorted(sexes)))
    lint_weights(doc.get("age_bands"), "age_bands")
    if tuple(row[0] for row in doc["age_bands"]) != AGE_BANDS:
        raise SystemExit("age_bands must be exactly %s, in that order" % (AGE_BANDS,))
    split = doc.get("young_to_middle")
    if type(split) is not float or not 0.0 < split < 1.0:
        raise SystemExit("young_to_middle must be a fraction strictly between 0 and 1")
    return doc


def lint_names(doc):
    supersede = doc.get("supersede")
    if not isinstance(supersede, dict) or not supersede.get("package"):
        raise SystemExit("names policy must record which package supersedes it")
    if supersede.get("status") != "complete":
        raise SystemExit("names policy records a handover that never completed")
    # INVERTED from every other absence rule in the port: the PRESENCE of this key is the
    # error. If race tables reappear, something has reintroduced the parent-race model.
    if "races" in doc:
        raise SystemExit("names policy carries race tables again; the genome supersedes them")
    return doc


def render_npcs():
    posts = lint_posts(load(POSTS, 1))
    names = lint_names(load(NAMES, 1))
    code = load(CODE_TABLES, 1)
    appearance = lint_appearance(load(APPEARANCE, 1), load(BODIES, 1))

    services, service_spans = [], {}
    for role in sorted(posts.get("services", {})):
        begin = len(services)
        services.extend(posts["services"][role])
        service_spans[role] = (begin, len(services) - begin)

    roles, role_spans = [], {}
    for building in sorted(posts["buildings"]):
        begin = len(roles)
        # FILE ORDER inside a building: this is seniority order and nothing re-sorts it.
        roles.extend(posts["buildings"][building])
        role_spans[building] = (begin, len(roles) - begin)

    verbs, verb_spans = [], {}
    for role in sorted(posts["role_verbs"]):
        begin = len(verbs)
        verbs.extend(posts["role_verbs"][role])
        verb_spans[role] = (begin, len(verbs) - begin)

    lines = [BANNER.format(source="%s, %s, %s and %s" % (POSTS, NAMES, CODE_TABLES, APPEARANCE))]
    lines.append('#include "fwg/npcs.hpp"\n')
    lines.append("namespace fwg {")
    lines.append("namespace {\n")

    lines.append("// The quest vocabulary, shared with the hero generator's hooks. Changing this")
    lines.append("// list desynchronises two packages, and the block exports it verbatim.")
    lines.append("constexpr std::string_view kVerbs[] = {%s};"
                 % "".join("%s, " % cpp_string(v) for v in posts["verbs"]))
    lines.append("")

    lines.append("// (role, target) pairs in SENIORITY order. The first row's first role is the")
    lines.append("// building's senior post and the only one the earmark can ever reach.")
    lines.append("constexpr NpcRole kRoles[] = {")
    for role, target in roles:
        lines.append("    {%s, %d}," % (cpp_string(role), target))
    lines.append("};\n")

    lines.append("// Sorted by id so the lookup is a binary search. This order reaches nothing:")
    lines.append("// `buildings` is only ever a lookup.")
    lines.append("constexpr NpcBuilding kBuildings[] = {")
    for building in sorted(posts["buildings"]):
        begin, count = role_spans[building]
        lines.append("    {%s, {kRoles + %d, %d}}," % (cpp_string(building), begin, count))
    lines.append("};\n")

    lines.append("constexpr std::string_view kRoleVerbs[] = {%s};"
                 % "".join("%s, " % cpp_string(v) for v in verbs))
    lines.append("")
    lines.append("// Sorted by role. The block re-sorts before exporting, so this order is the")
    lines.append("// exported one as well as the lookup one. A role ABSENT here can hold a post")
    lines.append("// and never be a giver, which is the same thing an empty list says.")
    lines.append("constexpr NpcRoleVerbs kRoleVerbRows[] = {")
    for role in sorted(posts["role_verbs"]):
        begin, count = verb_spans[role]
        lines.append("    {%s, {kRoleVerbs + %d, %d}}," % (cpp_string(role), begin, count))
    lines.append("};\n")

    lines.append("// THE INDEX IS THE RANK AND ZERO IS BEST. A building absent from this list is")
    lines.append("// never a quest giver, which is the exact opposite of rank 0.")
    lines.append("constexpr std::string_view kGiverRank[] = {")
    for building in posts["giver_rank"]:
        lines.append("    %s," % cpp_string(building))
    lines.append("};\n")

    lines.append("// Per site and AUTHORED, so the important set cannot drift upward when a")
    lines.append("// civilization's preset grows a building. Its boundedness is a contract: a")
    lines.append("// consumer scans this subset instead of walking the roster.")
    lines.append("constexpr NpcCap kCaps[] = {%s};"
                 % "".join("{%s, %d}, " % (cpp_string(k), v) for k, v in sorted(posts["caps"].items())))
    lines.append("")

    lines.append("// The eight blocks that name a place, in the order `places` walks them. FIRST")
    lines.append("// WRITER WINS on an id collision across blocks, so the order decides which")
    lines.append("// block a shared id resolves to.")
    lines.append("constexpr std::string_view kPlaceSources[] = {%s};"
                 % "".join("%s, " % cpp_string("%s.%s" % (block, key))
                           for block, key in code["place_sources"]))
    lines.append("")

    lines.append("// What a post offers the player beyond quest verbs, sorted by role.")
    lines.append("constexpr std::string_view kServices[] = {%s};"
                 % "".join("%s, " % cpp_string(v) for v in services))
    lines.append("constexpr NpcRoleServices kRoleServiceRows[] = {")
    for role in sorted(service_spans):
        begin, count = service_spans[role]
        lines.append("    {%s, {kServices + %d, %d}}," % (cpp_string(role), begin, count))
    lines.append("};\n")

    lines.append("// The baseline appearance weights, in file order: a person's draw walks them")
    lines.append("// cumulatively, so reordering either list re-maps every gender or age band.")
    lines.append("constexpr NpcWeight kGenders[] = {%s};"
                 % "".join("{%s, %d}, " % (cpp_string(g), w) for g, w in appearance["genders"]))
    lines.append("constexpr NpcWeight kAgeBands[] = {%s};"
                 % "".join("{%s, %d}, " % (cpp_string(b), w) for b, w in appearance["age_bands"]))
    lines.append("")

    lines.append("}  // namespace\n")
    lines.append("std::span<const NpcRoleServices> npc_role_services() { return kRoleServiceRows; }")
    lines.append("std::span<const NpcWeight> npc_genders() { return kGenders; }")
    lines.append("std::span<const NpcWeight> npc_age_band_weights() { return kAgeBands; }")
    lines.append("double npc_young_to_middle() { return %r; }" % appearance["young_to_middle"])
    lines.append("int npc_appearance_revision() { return %d; }" % revision_of(appearance, APPEARANCE))
    lines.append("std::span<const std::string_view> npc_verbs() { return kVerbs; }")
    lines.append("std::span<const NpcBuilding> npc_buildings() { return kBuildings; }")
    lines.append("std::span<const NpcRoleVerbs> npc_role_verbs() { return kRoleVerbRows; }")
    lines.append("std::span<const std::string_view> npc_giver_rank() { return kGiverRank; }")
    lines.append("std::span<const NpcCap> npc_caps() { return kCaps; }")
    lines.append("std::span<const std::string_view> npc_place_sources() { return kPlaceSources; }")
    lines.append("int npc_posts_revision() { return %d; }" % revision_of(posts, POSTS))
    lines.append("int npc_names_revision() { return %d; }" % revision_of(names, NAMES))
    lines.append("std::string_view npc_method() { return %s; }" % cpp_string(code["method"]))
    lines.append("std::string_view npc_limits() { return %s; }" % cpp_string(code["limits"]))
    lines.append("")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_npcs.cpp": render_npcs}
