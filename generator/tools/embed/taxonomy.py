"""data/terrain-taxonomy.json -> src/generated/content_taxonomy.cpp.

The portable terrain taxonomy, version 1: the identities a consumer may name. It is kept
independent of the generator's own tables on purpose -- a consumer resolves identities
without linking the generator -- and this module is where the two are made to agree:

  * `schools` must be the KNOWN schools of magic-schools.json, in their order. Hidden
    schools are not in taxonomy v1; adding them is a new taxonomy version.
  * `natural_biomes` must be biomes.json's ids and cores, in its order. Ids are sparse and
    are never array offsets.
  * `retired_natural_ids` must not collide with a live id: a retired id stays reserved.
  * `aliases` must be empty. The resolver supports no alias, so a table here would publish
    a mapping nothing honours.
"""
from embed.common import BANNER, OUT, load
from embed.json_text import json_text, text_function

SOURCE = "terrain-taxonomy.json"
SCHEMA = "fwg.terrain-taxonomy"
KEYS = ["schema", "taxonomy_version", "recipe_version", "schools", "natural_biomes", "retired_natural_ids", "aliases"]


def load_taxonomy():
    doc = load(SOURCE, 1)
    if doc.get("schema") != SCHEMA:
        raise SystemExit("%s must declare schema %r" % (SOURCE, SCHEMA))
    taxonomy = doc["taxonomy"]
    if list(taxonomy) != KEYS:
        raise SystemExit("%s: taxonomy keys must be exactly %s, in that order" % (SOURCE, KEYS))
    if taxonomy["schema"] != "fantasy-world-generator.terrain-taxonomy" or taxonomy["taxonomy_version"] != 1:
        raise SystemExit("%s: this is taxonomy version 1; a change of identity needs a new version" % SOURCE)
    if taxonomy["recipe_version"] != 3:
        raise SystemExit("%s: taxonomy v1 covers recipe 3 only" % SOURCE)

    schools = [row["name"] for row in load("magic-schools.json", 1)["schools"] if not row["hidden"]]
    if taxonomy["schools"] != schools:
        raise SystemExit("%s: schools %s are not magic-schools.json's known schools %s in order"
                         % (SOURCE, taxonomy["schools"], schools))

    biomes = [(row["id"], row["core"]) for row in load("biomes.json", 1)["natural_biomes"]]
    declared = [(row["id"], row["core"]) for row in taxonomy["natural_biomes"]]
    if any(set(row) != {"id", "core"} for row in taxonomy["natural_biomes"]):
        raise SystemExit("%s: a natural biome row is exactly id and core" % SOURCE)
    if declared != biomes:
        raise SystemExit("%s: natural_biomes %s disagree with biomes.json %s" % (SOURCE, declared, biomes))

    retired = taxonomy["retired_natural_ids"]
    if any(type(v) is not int for v in retired) or set(retired) & {i for i, _ in biomes}:
        raise SystemExit("%s: retired ids must be integers no live biome uses" % SOURCE)
    if taxonomy["aliases"] != {}:
        raise SystemExit("%s: the resolver supports no aliases, so the table must be empty" % SOURCE)
    return taxonomy


def render_taxonomy():
    taxonomy = load_taxonomy()
    lines = [BANNER.format(source=SOURCE)]
    lines.append('#include "fwg/product_content.hpp"\n')
    lines.append("#include <string>\n")
    lines.append("namespace fwg {\n")
    lines.extend(text_function("terrain_taxonomy_json", json_text(taxonomy), "the terrain taxonomy"))
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_taxonomy.cpp": render_taxonomy}
