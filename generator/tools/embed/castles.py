"""data/castles.json -> src/generated/content_castles.cpp.

TWO CONTRACTS LIVE IN THIS FILE, and they are different in kind.

  1. THE `kits` LIST ORDER IS AN RNG CONTRACT. A fortress's kit is one uniform draw
     walked cumulatively along the list, so swapping two kits -- even two of equal
     weight -- hands a different kit to the same fortress. The table is emitted in file
     order and nothing here sorts it. `modules` is keyed and read by name, so it is
     emitted sorted and its order is free.
  2. THE WHOLE REGISTRY IS HASHED, and the digest gates an age advance. The reference
     serialises it with sort_keys=True, the tightest separators and ensure_ascii left at
     its default, so key order and whitespace are free and every value and list order is
     not. That serialisation is NOT the heritage or civilization registries' scheme.
     This module embeds the canonical BYTES and C++ hashes them at runtime, for the reason
     city_shapes.py gives: a copied digest agrees with the data only until somebody edits
     the data. The digest data/castles.json records is checked here instead, so the file
     and its record cannot drift apart.

The reference's identity carries a second digest, over the castle library of the
BUILDING registry flattened to {id: structure}. That is another file's data, so it is
embedded from data/buildings.json on the same terms.

Validation is fail-closed, and stricter than the reference's loader, which checks only the
schema, its version and the revision. The reference resolves a court's, a landmark's or a
ring gate's name only when it builds one, so a name that does not exist raises in the
middle of a world; every name is resolved here instead, and the build fails with the
offender named. Also refused: a key the tables do not carry (it is inside the digest, so
something would be hashed that nothing can read), a weight below 1 (the reference silently
reads 0 as 1), a null walkway flag (the reference's `is False` test would exempt it), and
two rings of one role in a kit (their ids and the courts' lookup would collide).
"""
import hashlib
import json

from embed.common import BANNER, OUT, cpp_double, cpp_string, load

SOURCE = "castles.json"

REGISTRY_SCHEMA = "fantasy-world-generator.castle-registry"
MODULE_ROLES = ("curtain", "node", "court", "landmark")
# Every module the planner reaches by a literal key rather than through a kit. A registry
# without one of these cannot build a single ring.
REACHED_BY_NAME = ("curtain_segment", "corner_join", "postern", "wall_stair")

# The C++ constants these records must agree with. The recorded digest was taken with the
# planner at these versions, so a mismatch means the file and the code describe
# different identities.
PLANNER_VERSION = 3
GEOMETRY_VERSION = 1


def _canonical(document):
    """The exact bytes castle_planner.planner_identity() hashes.

    sort_keys=True, separators (',', ':'), ensure_ascii at its default. All three are
    load-bearing: the digest is compared before an age advance is allowed.
    """
    return json.dumps(document, sort_keys=True, separators=(",", ":"))


def _chunks(text, width=2000):
    return [text[i:i + width] for i in range(0, len(text), width)]


def _is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def _exact_keys(record, expected, where):
    keys = set(record)
    if keys != set(expected):
        raise SystemExit("%s: expected keys %s, got %s. Every key is inside the registry digest, "
                         "so a key the tables do not carry is a key nothing can read."
                         % (where, sorted(expected), sorted(keys)))


def _band(value, where, nullable):
    if value is None:
        if not nullable:
            raise SystemExit("%s must be a {min, max, nominal} band, not null" % where)
        return None
    _exact_keys(value, ("min", "max", "nominal"), where)
    if not all(_is_number(value[k]) for k in ("min", "max", "nominal")):
        raise SystemExit("%s has a non-numeric bound" % where)
    if not value["min"] <= value["nominal"] <= value["max"]:
        raise SystemExit("%s nominal lies outside its own band" % where)
    return value


def castle_structures():
    """{structure id: structure} over the building registry's castle library.

    The reference flattens block order then structure order into a dict; the digest is
    taken with sorted keys, so neither order reaches it.
    """
    buildings = load("buildings.json", 1)
    library = buildings["registry"]["structure_blocks"]["castle"]
    flat = {}
    for block in library["blocks"]:
        for structure in block["structures"]:
            if structure["id"] in flat:
                raise SystemExit("castle structure %s is listed twice" % structure["id"])
            flat[structure["id"]] = structure
    return flat


def load_castles():
    doc = load(SOURCE, 1)
    if doc.get("schema") != "fwg.castles":
        raise SystemExit("castles.json is not an fwg.castles document")
    registry = doc["registry"]

    # The reference loader's own three refusals, in its order.
    if registry.get("schema") != REGISTRY_SCHEMA or registry.get("schema_version") != 1:
        raise SystemExit("Unsupported castle registry schema")
    if not _is_int(registry.get("revision")) or registry["revision"] < 1:
        raise SystemExit("Invalid castle registry revision")

    identity = hashlib.sha256(_canonical(registry).encode()).hexdigest()
    if identity != doc["castles_sha256"]:
        raise SystemExit("castles.json castles_sha256 does not match the registry: recorded %s, "
                         "computed %s" % (doc["castles_sha256"], identity))
    if doc["planner_version"] != PLANNER_VERSION or doc["geometry_version"] != GEOMETRY_VERSION:
        raise SystemExit("castles.json records planner %s / geometry %s; this port is %d / %d"
                         % (doc["planner_version"], doc["geometry_version"],
                            PLANNER_VERSION, GEOMETRY_VERSION))

    _exact_keys(registry, ("schema", "schema_version", "revision", "description", "unit",
                           "join_rules", "modules", "kits"), "castle registry")
    if registry["unit"] != "metres":
        raise SystemExit("castle registry must be authored in metres")

    rules = registry["join_rules"]
    _exact_keys(rules, ("max_thickness_delta_m", "max_height_delta_m", "min_turn_degrees_for_corner",
                        "max_turn_degrees", "segment_depth_m", "walkway_required"), "join_rules")
    for key, value in rules.items():
        if key == "walkway_required":
            if not isinstance(value, bool):
                raise SystemExit("join_rules.walkway_required must be a boolean")
        elif not _is_number(value) or value < 0:
            raise SystemExit("join_rules.%s must be a non-negative number" % key)
    if rules["segment_depth_m"] < 0.1:
        # Cut points are rounded to the centimetre, so a shorter run rounds back onto the
        # point it started from and segmentation never advances. castle_segmentize refuses
        # the same value at runtime.
        raise SystemExit("join_rules.segment_depth_m must be at least 0.1 m")

    structures = castle_structures()
    modules = registry["modules"]
    for key, module in modules.items():
        where = "module %s" % key
        _exact_keys(module, ("id", "structure_id", "role", "end_type", "thickness_m", "height_m",
                             "walkway", "compatible_ends"), where)
        if not isinstance(module["id"], str) or not module["id"]:
            raise SystemExit("%s has no id" % where)
        if module["structure_id"] not in structures:
            raise SystemExit("%s stands for %s, which is not in the castle building library"
                             % (where, module["structure_id"]))
        if module["role"] not in MODULE_ROLES:
            raise SystemExit("%s has unknown role %r" % (where, module["role"]))
        # null is a real answer -- the module has no join end -- but an EMPTY string would
        # pass the reference's `is None` test and then match nothing.
        if module["end_type"] is not None and (not isinstance(module["end_type"], str)
                                               or not module["end_type"]):
            raise SystemExit("%s end_type must be a name or null" % where)
        _band(module["thickness_m"], where + " thickness_m", nullable=True)
        _band(module["height_m"], where + " height_m", nullable=False)
        # The reference tests walkway continuity by IDENTITY with False, so a missing or
        # null flag would silently exempt a module. A boolean is required instead.
        if not isinstance(module["walkway"], bool):
            raise SystemExit("%s walkway must be true or false" % where)
        if not isinstance(module["compatible_ends"], list) or not all(
                isinstance(e, str) and e for e in module["compatible_ends"]):
            raise SystemExit("%s compatible_ends must be a list of names" % where)
    for key in REACHED_BY_NAME:
        if key not in modules:
            raise SystemExit("the planner reaches module %s by name and the registry has none" % key)
    if modules["curtain_segment"]["thickness_m"] is None:
        raise SystemExit("the curtain's thickness is every wall join's thickness; it cannot be null")

    kits = registry["kits"]
    if not kits:
        raise SystemExit("castle registry has no kit")
    seen = set()
    for kit in kits:
        where = "kit %s" % kit.get("id")
        _exact_keys(kit, ("id", "name", "weight", "half_m", "rings", "courts", "landmarks", "services"),
                    where)
        if kit["id"] in seen:
            raise SystemExit("duplicate castle kit id %s" % kit["id"])
        seen.add(kit["id"])
        # The reference reads max(1, int(weight)), which makes a weight of 0 mean 1. Refused
        # rather than reproduced: a kit that cannot be drawn should not be quietly drawable.
        if not _is_int(kit["weight"]) or kit["weight"] < 1:
            raise SystemExit("%s weight must be an integer of at least 1" % where)
        if not _is_number(kit["half_m"]) or kit["half_m"] <= 0:
            raise SystemExit("%s half_m must be positive" % where)
        if not kit["rings"]:
            raise SystemExit("%s has no ring, so no castle of it could ever close" % where)
        roles = set()
        for ring in kit["rings"]:
            _exact_keys(ring, ("role", "scale", "ditch", "gate", "postern", "stairs", "tower_budget"),
                        where + " ring")
            # Ring ids are ring-<role> and the courts find their ring by role, so two rings
            # of one role would collide in both and silently lose a ring.
            if not isinstance(ring["role"], str) or not ring["role"] or ring["role"] in roles:
                raise SystemExit("%s has a missing or repeated ring role %r" % (where, ring["role"]))
            roles.add(ring["role"])
            if not _is_number(ring["scale"]) or ring["scale"] <= 0:
                raise SystemExit("%s ring %s scale must be positive" % (where, ring["role"]))
            if not isinstance(ring["ditch"], bool) or not isinstance(ring["postern"], bool):
                raise SystemExit("%s ring %s ditch and postern must be booleans" % (where, ring["role"]))
            if ring["gate"] not in modules:
                raise SystemExit("%s ring %s gate %r is not a module" % (where, ring["role"], ring["gate"]))
            for key in ("stairs", "tower_budget"):
                if not _is_int(ring[key]) or ring[key] < 0:
                    raise SystemExit("%s ring %s %s must be a non-negative integer"
                                     % (where, ring["role"], key))
        for court in kit["courts"]:
            _exact_keys(court, ("role", "ring_role", "structure_id"), where + " court")
            if court["ring_role"] not in roles:
                raise SystemExit("%s court names ring %s, which the kit does not have"
                                 % (where, court["ring_role"]))
            if court["structure_id"] not in structures:
                raise SystemExit("%s court builds unknown structure %s" % (where, court["structure_id"]))
        for landmark in kit["landmarks"]:
            _exact_keys(landmark, ("structure_id", "ring_role", "offset_scale"), where + " landmark")
            if landmark["ring_role"] not in roles:
                raise SystemExit("%s landmark names ring %s, which the kit does not have"
                                 % (where, landmark["ring_role"]))
            if landmark["structure_id"] not in structures:
                raise SystemExit("%s landmark builds unknown structure %s"
                                 % (where, landmark["structure_id"]))
            if not _is_number(landmark["offset_scale"]):
                raise SystemExit("%s landmark offset_scale must be a number" % where)
        for service in kit["services"]:
            _exact_keys(service, ("structure_id", "count"), where + " service")
            if service["structure_id"] not in structures:
                raise SystemExit("%s service builds unknown structure %s"
                                 % (where, service["structure_id"]))
            if not _is_int(service["count"]) or service["count"] < 1:
                raise SystemExit("%s service count must be a positive integer" % where)
    return doc, registry, structures


def _optional_band(value):
    if value is None:
        return "std::nullopt"
    return "CastleBand{%s, %s, %s}" % (cpp_double(value["min"]), cpp_double(value["max"]),
                                      cpp_double(value["nominal"]))


def _pool(name, type_name, rows, placeholder):
    # A zero-length C++ array is ill-formed, so an empty pool keeps one unused element
    # that every empty span then points at.
    lines = ["constexpr %s %s[] = {" % (type_name, name)]
    for row in rows or [placeholder]:
        lines.append("    %s," % row)
    lines.append("};\n")
    return lines


def render_castles():
    doc, registry, structures = load_castles()
    rules = registry["join_rules"]

    ends = []
    modules = []
    for key in sorted(registry["modules"]):
        m = registry["modules"][key]
        begin = len(ends)
        ends.extend(cpp_string(e) for e in m["compatible_ends"])
        modules.append("{%s, %s, %s, %s, %s, %s, %s, %s, {kEnds + %d, %d}}"
                       % (cpp_string(key), cpp_string(m["id"]), cpp_string(m["structure_id"]),
                          cpp_string(m["role"]), cpp_string(m["end_type"] or ""),
                          _optional_band(m["thickness_m"]), _optional_band(m["height_m"]),
                          "true" if m["walkway"] else "false", begin, len(ends) - begin))

    rings, courts, landmarks, services, kits = [], [], [], [], []
    for kit in registry["kits"]:
        ring_begin = len(rings)
        for r in kit["rings"]:
            rings.append("{%s, %s, %s, %s, %s, %d, %d}"
                         % (cpp_string(r["role"]), cpp_double(r["scale"]),
                            "true" if r["ditch"] else "false", cpp_string(r["gate"]),
                            "true" if r["postern"] else "false", r["stairs"], r["tower_budget"]))
        court_begin = len(courts)
        for c in kit["courts"]:
            courts.append("{%s, %s, %s}" % (cpp_string(c["role"]), cpp_string(c["ring_role"]),
                                             cpp_string(c["structure_id"])))
        landmark_begin = len(landmarks)
        for l in kit["landmarks"]:
            landmarks.append("{%s, %s, %s}" % (cpp_string(l["structure_id"]), cpp_string(l["ring_role"]),
                                                cpp_double(l["offset_scale"])))
        service_begin = len(services)
        for s in kit["services"]:
            services.append("{%s, %d}" % (cpp_string(s["structure_id"]), s["count"]))
        kits.append("    {%s, %s, %d, %s, {kRings + %d, %d}, {kCourts + %d, %d}, "
                    "{kLandmarks + %d, %d}, {kServices + %d, %d}},"
                    % (cpp_string(kit["id"]), cpp_string(kit["name"]), kit["weight"],
                       cpp_double(kit["half_m"]),
                       ring_begin, len(rings) - ring_begin,
                       court_begin, len(courts) - court_begin,
                       landmark_begin, len(landmarks) - landmark_begin,
                       service_begin, len(services) - service_begin))

    lines = [BANNER.format(source=SOURCE)]
    lines.append('#include "fwg/castle_plan.hpp"\n')
    lines.append('#include "fwg/sha256.hpp"\n')
    lines.append("#include <optional>")
    lines.append("#include <string>\n")
    lines.append("namespace fwg {\n")
    lines.append("// data/castles.json records the identity its digest was taken under. A planner at")
    lines.append("// another version publishes an identity that file never recorded.")
    lines.append("static_assert(kCastlePlanVersion == %d, \"data/castles.json records planner_version %d\");"
                 % (doc["planner_version"], doc["planner_version"]))
    lines.append("static_assert(kCastleGeometryVersion == %d, \"data/castles.json records geometry_version %d\");\n"
                 % (doc["geometry_version"], doc["geometry_version"]))
    lines.append("namespace {\n")

    lines.extend(_pool("kEnds", "std::string_view", ends, '""'))
    lines.append("// Sorted by key: every lookup is by name, so the order is free.")
    lines.extend(_pool("kModules", "CastleModule", modules, "{}"))
    lines.append("constexpr CastleJoinRules kJoinRules = {%s, %s, %s, %s, %s, %s};\n"
                 % (cpp_double(rules["max_thickness_delta_m"]), cpp_double(rules["max_height_delta_m"]),
                    cpp_double(rules["min_turn_degrees_for_corner"]), cpp_double(rules["max_turn_degrees"]),
                    cpp_double(rules["segment_depth_m"]),
                    "true" if rules["walkway_required"] else "false"))
    lines.extend(_pool("kRings", "CastleRingSpec", rings, "{}"))
    lines.extend(_pool("kCourts", "CastleCourtSpec", courts, "{}"))
    lines.extend(_pool("kLandmarks", "CastleLandmarkSpec", landmarks, "{}"))
    lines.extend(_pool("kServices", "CastleServiceSpec", services, "{}"))
    lines.append("// FILE ORDER. One uniform draw is walked cumulatively along this list, so a")
    lines.append("// reordered or extended list hands different fortresses different kits.")
    lines.append("constexpr CastleKit kKits[] = {")
    lines.extend(kits)
    lines.append("};\n")

    for name, text in (("kRegistryCanonical", _canonical(registry)),
                       ("kStructuresCanonical", _canonical(structures))):
        lines.append("constexpr std::string_view %s[] = {" % name)
        for chunk in _chunks(text):
            lines.append("    %s," % cpp_string(chunk))
        lines.append("};\n")

    lines.append("std::string joined(std::span<const std::string_view> chunks) {")
    lines.append("    std::string document;")
    lines.append("    for (std::string_view chunk : chunks) document.append(chunk);")
    lines.append("    return document;")
    lines.append("}\n")
    lines.append("}  // namespace\n")

    lines.append("std::span<const CastleModule> castle_modules() { return kModules; }\n")
    lines.append("const CastleJoinRules& castle_join_rules() { return kJoinRules; }\n")
    lines.append("std::span<const CastleKit> castle_kits() { return kKits; }\n")
    lines.append("int castle_registry_revision() { return %d; }\n" % registry["revision"])
    lines.append("// Hashed here rather than recorded, so the digest cannot outlive the bytes it")
    lines.append("// describes. The codegen has already refused bytes whose digest differs from the")
    lines.append("// one data/castles.json records.")
    lines.append("const std::string& castle_registry_identity() {")
    lines.append("    static const std::string identity = sha256_hex(joined(kRegistryCanonical));")
    lines.append("    return identity;")
    lines.append("}\n")
    lines.append("const std::string& castle_structures_identity() {")
    lines.append("    static const std::string identity = sha256_hex(joined(kStructuresCanonical));")
    lines.append("    return identity;")
    lines.append("}\n")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {
    OUT / "content_castles.cpp": render_castles,
}
