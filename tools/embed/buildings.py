"""data/buildings.json -> src/generated/content_buildings.cpp.

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

THE EIGHTH LAYOUT FEATURE. The reference drops one: _normalize_layout_profile and
_build_city_layout_plan each iterate their own hard-coded seven-name tuple, so a
feature added to a profile is ignored by both with no error at all. This port reads
the profile instead and REFUSES an unknown feature name here, at generation time. The
shipped profiles declare exactly the seven, so today's tables are the same tables; the
difference is that adding an eighth is a build failure with the offender named, rather
than a feature that silently does nothing in a world nobody can tell is wrong.
"""
import json

from embed.common import BANNER, DATA, OUT, cpp_double, cpp_string, optional_double

# The seven the reference iterates, with the fallback each takes when a profile omits
# it. Carried as DATA so a consumer reads the fallback rather than restating it.
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


def load_buildings():
    doc = json.loads((DATA / "buildings.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("buildings.json must declare schema version 1")
    registry = doc["registry"]

    packs = registry["building_packs"]["packs"]
    if not packs:
        raise SystemExit("buildings.json has no building pack")
    fallback = registry["building_packs"]["fallback_pack_id"]
    if fallback not in {p["id"] for p in packs}:
        raise SystemExit("the fallback pack %s is not in the pack list" % fallback)
    for pack in packs:
        if not pack["building_options"]:
            raise SystemExit("pack %s offers nothing" % pack["id"])
        for option in pack["building_options"]:
            if not option["asset_choices"]:
                raise SystemExit("option %s/%s has no asset choice" % (pack["id"], option["id"]))
            if any(c["weight"] <= 0 for c in option["asset_choices"]):
                # A zero-weight choice is unreachable in a cumulative walk, so it is not
                # a rarer asset, it is one that never appears.
                raise SystemExit("option %s/%s has a non-positive asset weight" % (pack["id"], option["id"]))

    for profile in registry["layout_profiles"]["profiles"]:
        unknown = sorted(set(profile["features"]) - set(LAYOUT_FEATURE_DEFAULTS))
        if unknown:
            raise SystemExit("layout profile %s declares feature(s) this port does not read: %s. "
                             "Add them to LAYOUT_FEATURE_DEFAULTS and to whatever consumes a "
                             "layout plan, or the feature does nothing."
                             % (profile["id"], unknown))

    seen = {}
    for library in LIBRARIES:
        block = registry["structure_blocks"][library]
        for group in block["blocks"]:
            for structure in group["structures"]:
                if structure["id"] in seen:
                    raise SystemExit("structure id %s appears in both %s and %s; the plan rows join "
                                     "on it across all three libraries and could not tell them apart"
                                     % (structure["id"], seen[structure["id"]], library))
                seen[structure["id"]] = library
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
    return doc, registry


def _box(d):
    return "{%s, %s, %s}" % (cpp_double(d["width"]), cpp_double(d["depth"]), cpp_double(d["height"]))


def _plot(d):
    return "{%s, %s}" % (cpp_double(d["width"]), cpp_double(d["depth"]))


def _feature_count(value, profiles_begin, profiles_len):
    if not isinstance(value, dict):
        value = {"base": value}
    return ("{%s, %s, %s, {kProfileNames + %d, %d}}"
            % (cpp_double(value.get("base", 0)),
               optional_double(value.get("per_100_residents")),
               optional_double(value.get("max")),
               profiles_begin, profiles_len))


def render_buildings():
    _, registry = load_buildings()

    lines = [BANNER.format(source="buildings.json")]
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
                choices.append("{%s, %s}" % (cpp_string(choice["asset_id"]), cpp_double(choice["weight"])))
            tag_begin, tag_len = text_span(option.get("tags", []))
            options.append(
                "    {%s, %s, %s, %s, %s, {kTexts + %d, %d}, {kAssetChoices + %d, %d}},"
                % (cpp_string(option["id"]), cpp_string(option["name"]),
                   _feature_count(option["count"], 0, 0),
                   "true" if option.get("required") else "false",
                   cpp_string(option.get("placement", "")),
                   tag_begin, tag_len, choice_begin, len(choices) - choice_begin))
        c = pack["criteria"]
        biome_begin, biome_len = int_span(c.get("biomes", []))
        variant_begin, variant_len = text_span(c.get("biome_variants", []))
        packs.append(
            "    {%s, %s, %s, %s, {{kBiomes + %d, %d}, {kTexts + %d, %d}, %s, %s, %s, %s, %s, %s}, "
            "{kBuildingOptions + %d, %d}},"
            % (cpp_string(pack["id"]), cpp_string(pack["name"]), cpp_string(pack["layout_profile_id"]),
               cpp_double(pack["weight"]), biome_begin, biome_len, variant_begin, variant_len,
               optional_double(c.get("freshwater_distance_m_min")),
               optional_double(c.get("freshwater_distance_m_max")),
               optional_double(c.get("height_min_m")),
               optional_double(c.get("slope_max_degrees")),
               optional_double(c.get("resource_min")),
               optional_double(c.get("resource_max")),
               option_begin, len(options) - option_begin))

    # --- layout profiles --------------------------------------------------
    profile_names = []
    features, profiles = [], []
    for name, spec in LAYOUT_FEATURE_DEFAULTS.items():
        begin = len(profile_names)
        profile_names.extend(cpp_string(v) for v in spec.get("profiles", []))
        features.append("    {%s, %s},  // default" % (cpp_string(name),
                                                       _feature_count(spec, begin, len(profile_names) - begin)))
    default_len = len(features)
    for profile in registry["layout_profiles"]["profiles"]:
        begin = len(features)
        # The profile's own key order, not a hard-coded tuple. See the module docstring.
        for name, spec in profile["features"].items():
            features.append("    {%s, %s}," % (cpp_string(name), _feature_count(spec, 0, 0)))
        placement = profile["placement"]
        profiles.append("    {%s, %s, {kLayoutFeatures + %d, %d}, {%s, %d, %s, %s}},"
                        % (cpp_string(profile["id"]), cpp_string(profile["name"]),
                           begin, len(features) - begin,
                           cpp_double(placement["river_buffer_m"]),
                           placement["river_fork_bridge_threshold"],
                           "true" if placement["bridge_if_river_fork"] else "false",
                           cpp_double(placement["max_anchor_slope_degrees"])))

    lines.append("constexpr std::string_view kProfileNames[] = {%s};\n"
                 % (", ".join(profile_names) if profile_names else '""'))
    # A zero-length C++ array is ill-formed, so an empty pool keeps one unused element
    # that every empty span then points at.
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
