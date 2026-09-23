"""data/hero-guild-policies.json -> src/generated/content_hero_guild.cpp.

The packaged hero guild policies. The same checks the reference's policy_document() makes
at load are made here, at generation time, so a bad edit is a build failure naming the
policy rather than a planner that refuses every request; the library makes them again at
runtime, because a caller-supplied policy goes through the same gate.

  * exactly schema, schema_version, revision, policies; schema version 1; revision >= 1;
  * one policy per civilization ENTITY of civilizations.json, and no other;
  * each policy exactly the eight fields, each a non-negative integer no larger than
    1,000,000,000 (booleans refused), with a positive denominator no smaller than the
    numerator, a party of 1..64 and at least one hero per service worker.
"""
from embed.common import BANNER, OUT, load
from embed.json_text import json_text, text_function

SOURCE = "hero-guild-policies.json"
SCHEMA = "fwg.hero-guild-policies"
MAX_COUNT = 1_000_000_000
POLICY_FIELDS = {"resident_rate_numerator", "resident_rate_denominator", "max_resident_heroes",
                 "max_visiting_heroes", "party_size", "max_active_parties", "heroes_per_service_worker",
                 "max_additional_staff"}


def count(value, where):
    if type(value) is not int or not 0 <= value <= MAX_COUNT:
        raise SystemExit("%s: %s must be an integer 0..%d" % (SOURCE, where, MAX_COUNT))


def load_policies():
    doc = load(SOURCE, 1)
    if doc.get("schema") != SCHEMA:
        raise SystemExit("%s must declare schema %r" % (SOURCE, SCHEMA))
    document = doc["document"]
    if set(document) != {"schema", "schema_version", "revision", "policies"}:
        raise SystemExit("%s: the policy document is exactly schema, schema_version, revision, policies" % SOURCE)
    if document["schema"] != "fantasy-world-generator.hero-guild-policies" or document["schema_version"] != 1 \
            or type(document["schema_version"]) is not int:
        raise SystemExit("%s: unsupported guild policy version" % SOURCE)
    count(document["revision"], "revision")
    if document["revision"] == 0:
        raise SystemExit("%s: policy revision must be positive" % SOURCE)
    entities = [p["id"] for p in load("civilizations.json", 1)["profiles"] if p["kind"] == "entity"]
    policies = document["policies"]
    if set(policies) != set(entities):
        raise SystemExit("%s: policies %s must cover exactly the civilization entities %s"
                         % (SOURCE, sorted(policies), sorted(entities)))
    for name, policy in policies.items():
        if set(policy) != POLICY_FIELDS:
            raise SystemExit("%s: policy %s must have exactly %s" % (SOURCE, name, sorted(POLICY_FIELDS)))
        for field, value in policy.items():
            count(value, name + "." + field)
        if (policy["resident_rate_denominator"] == 0
                or policy["resident_rate_numerator"] > policy["resident_rate_denominator"]
                or not 1 <= policy["party_size"] <= 64 or policy["heroes_per_service_worker"] == 0):
            raise SystemExit("%s: policy %s has an invalid rate or capacity" % (SOURCE, name))
    return document


def render_hero_guild():
    document = load_policies()
    lines = [BANNER.format(source=SOURCE)]
    lines.append('#include "fwg/product_content.hpp"\n')
    lines.append("#include <string>\n")
    lines.append("namespace fwg {\n")
    lines.extend(text_function("hero_guild_policies_json", json_text(document), "the hero guild policies"))
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_hero_guild.cpp": render_hero_guild}
