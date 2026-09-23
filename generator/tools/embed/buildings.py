"""data/buildings.json (+ settlement-plans.json bindings) -> src/generated/content_buildings.cpp.

THREE ORDER CONTRACTS LIVE HERE, all under `building_packs`, and all three are
preserved by emitting list order verbatim and sorting nothing:

  1. `packs` list order. The pack chooser filters the list and walks the survivors
     cumulatively against ONE random draw, with no sort in the path. Swapping two
     packs hands a different pack to the same city.
  2. `building_options` list order, twice over. The options share one rng stream and
     each consumes a variable number of draws, so reordering shifts every later
     option's asset draw; and an anchor cursor round-robins over the city's nodes per
     option, so reordering also changes WHICH NODE each building lands on.
  3. `asset_choices` order inside an option, walked cumulatively against one draw.

Append, never insert, in all three. `layout_profiles`, `structure_blocks` and
`housing_profiles` are id-keyed and order-free -- but their BYTES are still inside the
registry identity, so a reorder there retires every saved world even though no
consumer can see it.

WHO MAY BUILD WHAT is not in buildings.json at all. The reference's loader derives it
from each civilization's `buildings` bindings (settlement-plans.json here), and so does
this codegen, the way civilization_registry derives it:

  * building_pack_data(): a pack's `criteria.population_profiles` is every people whose
    `buildings.packs` names the pack, and an option's `profiles` is every people that
    lists the option under that pack. An option nobody lists is REMOVED there, because
    the reference's own option loader reads an empty list as "everybody"; here it is
    kept with an empty list, which the port reads as "nobody" -- the same effect, since
    a skipped option draws nothing and claims nothing.
  * layout_profile_data(): a layout feature's `profiles` is every people that lists the
    feature under that layout in `buildings.layout_features`. An empty list there DOES
    mean nobody, in the reference and here.

Each derived list is in the registry's civilization order and is read as a set. An
authored `population_profiles` or `profiles` key would be overwritten by that derivation
in the reference, so it is refused here rather than silently ignored.

THE EIGHTH LAYOUT FEATURE. The reference drops one: _normalize_layout_profile and
_build_city_layout_plan each iterate their own hard-coded seven-name tuple, so a
feature added to a profile is ignored by both with no error at all. This port refuses
an unknown feature name here, at generation time. The shipped profiles declare exactly
the seven, so today's tables are the same tables; the difference is that adding an
eighth is a build failure with the offender named, rather than a feature that silently
does nothing in a world nobody can tell is wrong.

Everything else _validate_building_packs and _load_city_layout_profiles check at load is
checked here, with their defaults applied: an absent placement is `anchored`, an absent
weight is 1, absent layout placement values are 30 m / 2 / true / 12 degrees.
"""
import json
import math

from embed.common import BANNER, DATA, OUT, cpp_double, cpp_string, optional_double

# The seven the reference iterates, IN ITS ORDER, with the fallback each takes when a
# profile omits it. Carried as DATA so a consumer reads the fallback rather than restating
# it. The order is the district pass's draw order: a feature's anchors come off the rotated
# node list after every feature before it.
LAYOUT_FEATURE_DEFAULTS = {
    "leader_homes": {"base": 1},
    "barracks": {"base": 1},
    "noble_homes": {"base": 1, "per_100_residents": 0.04},
    "worker_housing": {"base": 2, "per_100_residents": 0.25},
    "apartments": {"base": 0, "per_100_residents": 0.15, "max": 5},
    "market_district": {"base": 1, "max": 3},
    "religious_building": {"base": 1, "profiles": ["human", "elf", "gnome", "dwarf", "tidekin"], "max": 2},
}

PRIORITIES = ("core", "conditional", "specialist")
LIBRARIES = ("common", "rural", "castle")
PLACEMENTS = ("anchored", "structural", "river_gate", "wall")
PACK_KEYS = {"id", "name", "layout_profile_id", "criteria", "weight", "building_options"}
CRITERIA_KEYS = {"biomes", "biome_variants", "height_min_m", "height_max_m", "resource_min",
                 "resource_max", "slope_max_degrees", "freshwater_distance_m_min",
                 "freshwater_distance_m_max"}
OPTION_KEYS = {"id", "name", "count", "required", "placement", "requires_bridge", "tags",
               "asset_choices"}
COUNT_KEYS = {"base", "per_100_residents", "min", "max"}
PLACEMENT_KEYS = {"river_buffer_m", "river_fork_bridge_threshold", "bridge_if_river_fork",
                  "max_anchor_slope_degrees"}


def _finite(value, what):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise SystemExit("%s must be a finite number, got %r" % (what, value))
    return float(value)


def _integer(value, what):
    if isinstance(value, bool) or not isinstance(value, int):
        raise SystemExit("%s must be an integer, got %r" % (what, value))
    return value


def _flag(value, what):
    if not isinstance(value, bool):
        raise SystemExit("%s must be a boolean, got %r" % (what, value))
    return value


def _check_count(value, what, allow_profiles):
    """_coerce_feature_count's rules. Returns the count as a dict."""
    if not isinstance(value, dict):
        _finite(value, what)
        return {"base": value}
    unknown = sorted(set(value) - COUNT_KEYS - ({"profiles"} if allow_profiles else set()))
    if unknown:
        raise SystemExit("%s names key(s) nothing reads: %s" % (what, unknown))
    _finite(value.get("base", 0), what + ".base")
    per_100 = value.get("per_100_residents", 0)
    if _finite(per_100, what + ".per_100_residents") < 0:
        raise SystemExit("%s.per_100_residents must be non-negative" % what)
    low, high = value.get("min"), value.get("max")
    if low is not None:
        _integer(low, what + ".min")
    if high is not None:
        _integer(high, what + ".max")
    if low is not None and high is not None and low > high:
        raise SystemExit("%s.min cannot exceed %s.max" % (what, what))
    return value


def _bindings(registry):
    """Who may build what: each people's `buildings` bindings, checked as the registry checks them."""
    plans = json.loads((DATA / "settlement-plans.json").read_text(encoding="utf-8"))
    packs = {p["id"]: [o["id"] for o in p["building_options"]] for p in registry["building_packs"]["packs"]}
    layouts = {p["id"]: set(p["features"]) for p in registry["layout_profiles"]["profiles"]}
    fallback = registry["building_packs"]["fallback_pack_id"]
    bindings = []
    for entity in plans["entities"]:
        bound = entity["buildings"]
        for pack, options in bound["packs"].items():
            if pack not in packs or not isinstance(options, list) or len(options) != len(set(options)) \
                    or not set(options) <= set(packs[pack]):
                raise SystemExit("%s binds an unknown or duplicate building reference under %s"
                                 % (entity["id"], pack))
        if fallback not in bound["packs"]:
            raise SystemExit("%s does not bind the fallback pack %s" % (entity["id"], fallback))
        for layout, features in bound.get("layout_features", {}).items():
            if layout not in layouts or not set(features) <= layouts[layout]:
                raise SystemExit("%s binds an unknown layout feature reference under %s"
                                 % (entity["id"], layout))
        bindings.append((entity["id"], bound))
    return bindings


def load_buildings():
    doc = json.loads((DATA / "buildings.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("buildings.json must declare schema version 1")
    registry = doc["registry"]

    biomes = json.loads((DATA / "biomes.json").read_text(encoding="utf-8"))
    natural_ids = {row["id"] for row in biomes["natural_biomes"]}
    variant_ids = {"%s.%s" % (row["core"], school)
                   for row in biomes["natural_biomes"] for school in biomes["variant_names"]}

    library = registry["building_packs"]
    if library.get("schema_version") != 3:
        raise SystemExit("building packs require schema 3")
    packs = library["packs"]
    if not packs:
        raise SystemExit("buildings.json has no building pack")
    if len({p["id"] for p in packs}) != len(packs):
        raise SystemExit("duplicate building pack id")
    fallback = library["fallback_pack_id"]
    if fallback not in {p["id"] for p in packs}:
        raise SystemExit("the fallback pack %s is not in the pack list" % fallback)

    layouts = registry["layout_profiles"]
    if not isinstance(layouts.get("schema_version"), int):
        raise SystemExit("layout profiles need an integer schema_version")
    layout_ids = [p["id"] for p in layouts["profiles"]]
    if not layout_ids or len(set(layout_ids)) != len(layout_ids):
        raise SystemExit("layout profiles must be a non-empty list with unique ids")
    if layouts["fallback_profile_id"] not in layout_ids:
        raise SystemExit("the fallback layout profile %s is not a profile" % layouts["fallback_profile_id"])

    for pack in packs:
        where = "pack %s" % pack["id"]
        unknown = sorted(set(pack) - PACK_KEYS)
        if unknown:
            raise SystemExit("%s names key(s) nothing reads: %s" % (where, unknown))
        if pack.get("layout_profile_id") is not None and pack["layout_profile_id"] not in layout_ids:
            raise SystemExit("%s names unknown layout profile %s" % (where, pack["layout_profile_id"]))
        if _finite(pack.get("weight", 1.0), where + " weight") <= 0:
            raise SystemExit("%s weight must be positive" % where)
        criteria = pack["criteria"]
        if not isinstance(criteria, dict):
            raise SystemExit("%s is missing criteria" % where)
        if "population_profiles" in criteria:
            raise SystemExit("%s authors population_profiles, which the civilizations' own bindings "
                             "decide; the reference overwrites it at load" % where)
        unknown = sorted(set(criteria) - CRITERIA_KEYS)
        if unknown:
            raise SystemExit("%s criteria name key(s) nothing reads: %s" % (where, unknown))
        ground = criteria.get("biomes", [])
        if not isinstance(ground, list) or any(type(b) is not int or b not in natural_ids for b in ground):
            raise SystemExit("unknown natural biome in %s" % where)
        variants = criteria.get("biome_variants", [])
        if not isinstance(variants, list) or any(not isinstance(v, str) or v not in variant_ids for v in variants):
            raise SystemExit("unknown magical biome in %s" % where)
        for key in CRITERIA_KEYS - {"biomes", "biome_variants"}:
            if criteria.get(key) is not None:
                _finite(criteria[key], "%s %s" % (where, key))

        if not pack["building_options"]:
            raise SystemExit("pack %s offers nothing" % pack["id"])
        if len({o["id"] for o in pack["building_options"]}) != len(pack["building_options"]):
            raise SystemExit("%s has a duplicate building option id" % where)
        for option in pack["building_options"]:
            what = "option %s/%s" % (pack["id"], option["id"])
            if "profiles" in option:
                raise SystemExit("%s authors profiles, which the civilizations' own bindings decide; "
                                 "the reference overwrites it at load" % what)
            unknown = sorted(set(option) - OPTION_KEYS)
            if unknown:
                raise SystemExit("%s names key(s) nothing reads: %s" % (what, unknown))
            # An option's own count may not carry `profiles` either: the reference reads it
            # only when the option has no derived list, which it always has.
            _check_count(option.get("count", {"base": 0}), what + " count", allow_profiles=False)
            _flag(option.get("required", False), what + " required")
            _flag(option.get("requires_bridge", False), what + " requires_bridge")
            if option.get("placement", "anchored") not in PLACEMENTS:
                raise SystemExit("%s placement must be one of %s" % (what, list(PLACEMENTS)))
            tags = option.get("tags", [])
            if not isinstance(tags, list) or any(not isinstance(t, str) or not t for t in tags):
                raise SystemExit("%s tags must be a list of strings" % what)
            if not option["asset_choices"]:
                raise SystemExit("option %s/%s has no asset choice" % (pack["id"], option["id"]))
            for choice in option["asset_choices"]:
                if not isinstance(choice.get("asset_id"), str) or not choice["asset_id"]:
                    raise SystemExit("%s has an asset choice with no asset_id" % what)
                if _finite(choice.get("weight", 1.0), what + " weight") <= 0:
                    # A zero-weight choice is unreachable in a cumulative walk, so it is not
                    # a rarer asset, it is one that never appears.
                    raise SystemExit("option %s/%s has a non-positive asset weight" % (pack["id"], option["id"]))

    for profile in layouts["profiles"]:
        features = profile.get("features")
        if not isinstance(features, dict) or not features:
            raise SystemExit("layout profile %s is missing features" % profile["id"])
        unknown = sorted(set(features) - set(LAYOUT_FEATURE_DEFAULTS))
        if unknown:
            raise SystemExit("layout profile %s declares feature(s) this port does not read: %s. "
                             "Add them to LAYOUT_FEATURE_DEFAULTS and to whatever consumes a "
                             "layout plan, or the feature does nothing."
                             % (profile["id"], unknown))
        for name, spec in features.items():
            if isinstance(spec, dict) and "profiles" in spec:
                raise SystemExit("layout profile %s feature %s authors profiles, which the "
                                 "civilizations' own bindings decide; the reference overwrites "
                                 "it at load" % (profile["id"], name))
            _check_count(spec, "layout profile %s feature %s" % (profile["id"], name), allow_profiles=False)
        placement = profile.get("placement") or {}
        if not isinstance(placement, dict):
            raise SystemExit("layout profile %s placement must be an object" % profile["id"])
        unknown = sorted(set(placement) - PLACEMENT_KEYS)
        if unknown:
            raise SystemExit("layout profile %s placement names key(s) nothing reads: %s"
                             % (profile["id"], unknown))
        if _finite(placement.get("river_buffer_m", 30), "river_buffer_m") < 0:
            raise SystemExit("layout profile %s river_buffer_m must be non-negative" % profile["id"])
        _integer(placement.get("river_fork_bridge_threshold", 2), "river_fork_bridge_threshold")
        _finite(placement.get("max_anchor_slope_degrees", 12), "max_anchor_slope_degrees")

    seen = {}
    for library_key in LIBRARIES:
        block = registry["structure_blocks"][library_key]
        for group in block["blocks"]:
            for structure in group["structures"]:
                if structure["id"] in seen:
                    raise SystemExit("structure id %s appears in both %s and %s; the plan rows join "
                                     "on it across all three libraries and could not tell them apart"
                                     % (structure["id"], seen[structure["id"]], library_key))
                seen[structure["id"]] = library_key
                if structure["priority"] not in PRIORITIES:
                    raise SystemExit("structure %s has unknown priority %r"
                                     % (structure["id"], structure["priority"]))

    for key, profile in registry["housing_profiles"].items():
        if profile["worker_beds"] <= 0:
            raise SystemExit("housing profile %s sleeps nobody" % key)
    housing = registry["housing_profiles"]
    if housing["worker_apartment"]["plot_m"] != housing["worker_house"]["plot_m"]:
        # In-place densification re-uses the house's plot and corridor without re-testing
        # terrain. A different plot makes that silently illegal.
        raise SystemExit("worker_apartment must sit on the worker_house plot; densification "
                         "replaces a dwelling in place and never re-tests the ground")
    if housing["worker_apartment"]["worker_beds"] <= housing["worker_house"]["worker_beds"]:
        raise SystemExit("worker_apartment must sleep more than worker_house, or upgrading loses beds")
    return doc, registry, _bindings(registry)


def _box(d):
    return "{%s, %s, %s}" % (cpp_double(d["width"]), cpp_double(d["depth"]), cpp_double(d["height"]))


def _plot(d):
    return "{%s, %s}" % (cpp_double(d["width"]), cpp_double(d["depth"]))


def render_buildings():
    _, registry, bindings = load_buildings()

    lines = [BANNER.format(source="buildings.json and data/settlement-plans.json")]
    lines.append('#include "fwg/city_plan.hpp"\n')
    lines.append("namespace fwg {")
    lines.append("namespace {\n")

    # --- structures -------------------------------------------------------
    roles = []
    structures = []
    for library in LIBRARIES:
        block = registry["structure_blocks"][library]
        for group in block["blocks"]:
            for s in group["structures"]:
                begin = len(roles)
                target = 0
                for role in s["staffing"]["roles"]:
                    roles.append("{%s, %d, %d, %d}" % (cpp_string(role["role"]), role["minimum"],
                                                       role["target"], role["maximum"]))
                    target += role["target"]
                clearance = s["clearance_m"]
                structures.append(
                    "    {%s, %s, %s, %s, StructurePriority::%s, %s, %s, %s, %s, {%s, %s, %s, %s}, "
                    "{kRoles + %d, %d}, %d},"
                    % (cpp_string(s["id"]), cpp_string(s["name"]), cpp_string(library),
                       cpp_string(group["id"]), s["priority"], cpp_string(s["module_family"]),
                       cpp_string(s["geometry_type"]), _box(s["dimensions_m"]), _plot(s["plot_m"]),
                       cpp_double(clearance["front"]), cpp_double(clearance["rear"]),
                       cpp_double(clearance["left"]), cpp_double(clearance["right"]),
                       begin, len(roles) - begin, target))

    lines.append("constexpr StaffingRole kRoles[] = {")
    for role in roles:
        lines.append("    %s," % role)
    lines.append("};\n")
    lines.append("// The three libraries end to end, in catalogue order. Ids are unique ACROSS")
    lines.append("// libraries, which is what lets a plan row name one without naming its library.")
    lines.append("constexpr Structure kStructures[] = {")
    lines.extend(structures)
    lines.append("};\n")

    # --- housing ----------------------------------------------------------
    lines.append("constexpr HousingProfile kHousing[] = {")
    for key in sorted(registry["housing_profiles"]):
        p = registry["housing_profiles"][key]
        lines.append("    {%s, %s, %s, %s, %s, %d},"
                     % (cpp_string(key), cpp_string(p["id"]), cpp_string(p["name"]),
                        _box(p["dimensions_m"]), _plot(p["plot_m"]), p["worker_beds"]))
    lines.append("};\n")

    # --- who may build what -----------------------------------------------
    # One pool of civilization-id lists, each distinct list stored once.
    peoples, interned = [], {}

    def people_span(ids):
        key = tuple(ids)
        if key not in interned:
            interned[key] = (len(peoples), len(key))
            peoples.extend(cpp_string(v) for v in key)
        return interned[key]

    def people_literal(ids):
        begin, length = people_span(ids)
        return "{kPeoples + %d, %d}" % (begin, length)

    def pack_people(pack_id):
        return [key for key, bound in bindings if pack_id in bound["packs"]]

    def option_people(pack_id, option_id):
        return [key for key, bound in bindings if option_id in bound["packs"].get(pack_id, [])]

    def feature_people(layout_id, feature):
        return [key for key, bound in bindings if feature in bound.get("layout_features", {}).get(layout_id, [])]

    def feature_count(value, profiles):
        """A FeatureCount literal. `profiles` None is every people; a list is exactly those."""
        if not isinstance(value, dict):
            value = {"base": value}
        restricted = "std::nullopt" if profiles is None else \
            "std::span<const std::string_view>(kPeoples + %d, %d)" % people_span(profiles)
        return ("{%s, %s, %s, %s, %s}"
                % (cpp_double(value.get("base", 0)),
                   optional_double(value.get("per_100_residents")),
                   optional_double(value.get("min")),
                   optional_double(value.get("max")),
                   restricted))

    # --- packs ------------------------------------------------------------
    texts, choices, options, criteria_biomes = [], [], [], []
    packs = []

    def text_span(values):
        begin = len(texts)
        texts.extend(cpp_string(v) for v in values)
        return begin, len(texts) - begin

    def int_span(values):
        begin = len(criteria_biomes)
        criteria_biomes.extend(str(int(v)) for v in values)
        return begin, len(criteria_biomes) - begin

    for pack in registry["building_packs"]["packs"]:
        option_begin = len(options)
        for option in pack["building_options"]:
            choice_begin = len(choices)
            for choice in option["asset_choices"]:
                choices.append("{%s, %s}" % (cpp_string(choice["asset_id"]), cpp_double(choice.get("weight", 1.0))))
            tag_begin, tag_len = text_span(option.get("tags", []))
            options.append(
                "    {%s, %s, %s, %s, %s, %s, {kTexts + %d, %d}, {kAssetChoices + %d, %d}, %s},"
                % (cpp_string(option["id"]), cpp_string(option.get("name", option["id"])),
                   feature_count(option.get("count", {"base": 0}), None),
                   "true" if option.get("required", False) else "false",
                   "true" if option.get("requires_bridge", False) else "false",
                   cpp_string(option.get("placement", "anchored")),
                   tag_begin, tag_len, choice_begin, len(choices) - choice_begin,
                   people_literal(option_people(pack["id"], option["id"]))))
        c = pack["criteria"]
        biome_begin, biome_len = int_span(c.get("biomes", []))
        variant_begin, variant_len = text_span(c.get("biome_variants", []))
        # The reference gates a pack on ground whenever EITHER list is named, even empty.
        gated = "biomes" in c or "biome_variants" in c
        packs.append(
            "    {%s, %s, %s, %s, {%s, {kBiomes + %d, %d}, {kTexts + %d, %d}, %s, %s, %s, %s, %s, %s, %s, %s}, "
            "{kBuildingOptions + %d, %d}},"
            % (cpp_string(pack["id"]), cpp_string(pack.get("name", pack["id"])),
               cpp_string(pack.get("layout_profile_id") or ""),
               cpp_double(pack.get("weight", 1.0)),
               "true" if gated else "false",
               biome_begin, biome_len, variant_begin, variant_len,
               optional_double(c.get("freshwater_distance_m_min")),
               optional_double(c.get("freshwater_distance_m_max")),
               optional_double(c.get("height_min_m")),
               optional_double(c.get("height_max_m")),
               optional_double(c.get("slope_max_degrees")),
               optional_double(c.get("resource_min")),
               optional_double(c.get("resource_max")),
               people_literal(pack_people(pack["id"])),
               option_begin, len(options) - option_begin))

    # --- layout profiles --------------------------------------------------
    features, profiles = [], []
    for name, spec in LAYOUT_FEATURE_DEFAULTS.items():
        # A default is used only where a profile omits the feature, and it is NOT derived
        # from the bindings: the reference applies the default table verbatim, profiles and
        # all -- including five people ids that name no civilization in this registry.
        features.append("    {%s, %s},  // default" % (cpp_string(name), feature_count(spec, spec.get("profiles"))))
    default_len = len(features)
    for profile in registry["layout_profiles"]["profiles"]:
        begin = len(features)
        # The profile's own key order. The plan walks the seven in the defaults' order and
        # looks each one up here by name.
        for name, spec in profile["features"].items():
            features.append("    {%s, %s}," % (cpp_string(name),
                                               feature_count(spec, feature_people(profile["id"], name))))
        placement = profile.get("placement") or {}
        profiles.append("    {%s, %s, {kLayoutFeatures + %d, %d}, {%s, %d, %s, %s}},"
                        % (cpp_string(profile["id"]), cpp_string(profile.get("name", profile["id"])),
                           begin, len(features) - begin,
                           cpp_double(placement.get("river_buffer_m", 30)),
                           placement.get("river_fork_bridge_threshold", 2),
                           "true" if placement.get("bridge_if_river_fork", True) else "false",
                           cpp_double(placement.get("max_anchor_slope_degrees", 12))))

    # A zero-length C++ array is ill-formed, so an empty pool keeps one unused element
    # that every empty span then points at.
    lines.append("// Civilization-id lists, each distinct list once: who may draw a pack, build an")
    lines.append("// option or claim a layout feature. Derived from settlement-plans.json bindings.")
    lines.append("constexpr std::string_view kPeoples[] = {%s};\n" % (", ".join(peoples) if peoples else '""'))
    lines.append("constexpr std::string_view kTexts[] = {%s};\n" % (", ".join(texts) if texts else '""'))
    lines.append("constexpr int kBiomes[] = {%s};\n"
                 % (", ".join(criteria_biomes) if criteria_biomes else "0"))
    lines.append("constexpr AssetChoice kAssetChoices[] = {%s};\n" % ", ".join(choices))
    lines.append("// ORDER CONTRACT: one shared rng stream and one anchor cursor walk this list,")
    lines.append("// so an insertion moves every later option's asset AND its node. Append only.")
    lines.append("constexpr BuildingOption kBuildingOptions[] = {")
    lines.extend(options)
    lines.append("};\n")
    lines.append("// ORDER CONTRACT: filtered, then walked cumulatively against one draw.")
    lines.append("constexpr BuildingPack kBuildingPacks[] = {")
    lines.extend(packs)
    lines.append("};\n")
    lines.append("constexpr LayoutFeature kLayoutFeatures[] = {")
    lines.extend(features)
    lines.append("};\n")
    lines.append("constexpr LayoutProfile kLayoutProfiles[] = {")
    lines.extend(profiles)
    lines.append("};\n")
    lines.append("}  // namespace\n")

    lines.append("std::span<const Structure> structures() { return kStructures; }\n")
    lines.append("std::span<const HousingProfile> housing_profiles() { return kHousing; }\n")
    lines.append("std::span<const BuildingPack> building_packs() { return kBuildingPacks; }\n")
    lines.append("std::string_view fallback_building_pack_id() { return %s; }\n"
                 % cpp_string(registry["building_packs"]["fallback_pack_id"]))
    lines.append("int building_pack_schema_version() { return %d; }\n"
                 % registry["building_packs"]["schema_version"])
    lines.append("std::span<const LayoutProfile> layout_profiles() { return kLayoutProfiles; }\n")
    lines.append("std::string_view fallback_layout_profile_id() { return %s; }\n"
                 % cpp_string(registry["layout_profiles"]["fallback_profile_id"]))
    lines.append("std::span<const LayoutFeature> layout_feature_defaults() {")
    lines.append("    return std::span<const LayoutFeature>(kLayoutFeatures, %d);" % default_len)
    lines.append("}\n")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {
    OUT / "content_buildings.cpp": render_buildings,
}
