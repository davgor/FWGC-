"""Everything the exhaustive asset list compiles -> src/generated/content_asset_sources.cpp.

The asset list (src/asset_list.cpp) is a port of the reference's compile_asset_list, and it
publishes a content_sha256 over rows whose metadata is copied verbatim from authored data:
a creature row carries its whole profile, a production row its whole brief. The hash is
only reproducible if those rows are byte-for-byte what the reference compiled, so they are
embedded as the authored JSON itself -- key order and 1-versus-1.0 intact -- rather than
through the typed tables, which keep neither. The sources, all already under data/:

  creature_profiles        nest-profiles.json `profiles`, FILE order (not the generator's
                           animals-then-monsters order)
  civilization_ids         the civilization entities of civilizations.json, in order
  building_packs           buildings.json `registry.building_packs`
  entity_building_packs    each entity's `buildings.packs` binding, settlement-plans.json
  structure_blocks         buildings.json `registry.structure_blocks`
  housing_profiles         buildings.json `registry.housing_profiles`
  key_locations            key-locations-catalogue.json revision, families, archetypes
  key_location_exteriors   key-location-exteriors.json revision, parts
  production_catalogue     world-asset-requirements.json `catalogue` schema_version, assets

Checked here as the reference's loaders check them: ids unique within each source, the
entity bindings naming only packs and options that exist, and the production catalogue at
schema version 2. The selector and tombstone checks the reference makes while compiling
are made by the C++ compiler, which is the port of them.
"""
from embed.common import BANNER, OUT, load
from embed.json_text import json_text, text_function


def unique(ids, what):
    if len(set(ids)) != len(ids):
        seen, dupes = set(), set()
        for i in ids:
            (dupes if i in seen else seen).add(i)
        raise SystemExit("%s has duplicate ids %s" % (what, sorted(dupes)))


def load_sources():
    nests = load("nest-profiles.json", 1)
    buildings = load("buildings.json", 1)["registry"]
    plans = load("settlement-plans.json", 1)
    civilizations = load("civilizations.json", 1)
    catalogue = load("key-locations-catalogue.json", 1)
    exteriors = load("key-location-exteriors.json", 1)
    production = load("world-asset-requirements.json", 1)["catalogue"]

    entities = [p["id"] for p in civilizations["profiles"] if p["kind"] == "entity"]
    if [e["id"] for e in plans["entities"]] != entities:
        raise SystemExit("settlement-plans.json entities are not civilizations.json's entities in order")

    unique([p["id"] for p in nests["profiles"]], "nest-profiles.json profiles")
    packs = buildings["building_packs"]["packs"]
    unique([p["id"] for p in packs], "building packs")
    options = {p["id"]: {o["id"] for o in p["building_options"]} for p in packs}
    bindings = {}
    for entity in plans["entities"]:
        bound = entity["buildings"]["packs"]
        for pack, chosen in bound.items():
            if pack not in options or not set(chosen) <= options[pack]:
                raise SystemExit("entity %s binds an unknown pack or option in %s" % (entity["id"], pack))
        bindings[entity["id"]] = bound
    structures = [s["id"] for library in buildings["structure_blocks"].values()
                  for block in library["blocks"] for s in block["structures"]]
    unique(structures, "structure blocks")
    unique([a["id"] for a in catalogue["archetypes"]], "key-location archetypes")
    unique([p["id"] for p in exteriors["parts"]], "key-location exterior parts")
    if production.get("schema_version") != 2:
        raise SystemExit("world-asset-requirements.json: the production catalogue requires schema 2")
    unique([row["id"] for row in production["assets"]], "production catalogue rows")

    return {
        "creature_profiles": nests["profiles"],
        "civilization_ids": entities,
        "building_packs": buildings["building_packs"],
        "entity_building_packs": bindings,
        "structure_blocks": buildings["structure_blocks"],
        "housing_profiles": buildings["housing_profiles"],
        "key_locations": {"revision": catalogue["source_identity"]["revision"],
                          "families": catalogue["families"], "archetypes": catalogue["archetypes"]},
        "key_location_exteriors": {"revision": exteriors["source_identity"]["revision"],
                                   "parts": exteriors["parts"]},
        "production_catalogue": {"schema_version": production["schema_version"], "assets": production["assets"]},
    }


def render_asset_sources():
    sources = load_sources()
    lines = [BANNER.format(source="nest-profiles.json, buildings.json, settlement-plans.json, civilizations.json, "
                                  "key-locations-catalogue.json, key-location-exteriors.json and "
                                  "world-asset-requirements.json")]
    lines.append('#include "fwg/product_content.hpp"\n')
    lines.append("#include <string>\n")
    lines.append("namespace fwg {\n")
    lines.extend(text_function("asset_sources_json", json_text(sources), "the asset list's sources"))
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_asset_sources.cpp": render_asset_sources}
