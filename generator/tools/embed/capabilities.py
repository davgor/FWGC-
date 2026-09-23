"""data/capabilities.json -> src/generated/content_capabilities.cpp.

The producer capability descriptor this library publishes. It is NOT a copy of the
reference's: a descriptor's supported inventory must track the producer that answers
(Contracts/capabilities-and-coordinates.md in the reference), and this is a different
producer. So the file carries both, and this module refuses any difference between them
that `deviations` does not name -- and any difference at all in the two sections that are
contracts rather than inventory, `kernel` and the coordinate semantics (a documented prose
fix inside `coordinates` is the only exception).

Validation also mirrors what a consumer relies on: every supported entry is a non-empty
list of distinct integer versions, and nothing is both supported and unsupported.
"""
from embed.common import BANNER, OUT, load
from embed.json_text import json_text, text_function

SOURCE = "capabilities.json"
SCHEMA = "fwg.capabilities"


def leaves(value, prefix=""):
    """Every non-object value under `value`, keyed by dotted path."""
    if isinstance(value, dict):
        out = {}
        for key, item in value.items():
            out.update(leaves(item, prefix + key + "."))
        return out
    return {prefix[:-1]: value}


def load_descriptor():
    doc = load(SOURCE, 1)
    if doc.get("schema") != SCHEMA:
        raise SystemExit("%s must declare schema %r" % (SOURCE, SCHEMA))
    descriptor, reference, deviations = doc["descriptor"], doc["reference_descriptor"], doc["deviations"]

    if list(descriptor) != list(reference):
        raise SystemExit("%s: the descriptor's top-level keys differ from the reference's: %s vs %s"
                         % (SOURCE, list(descriptor), list(reference)))
    if descriptor["schema"] != "fantasy-world-generator.capabilities" or descriptor["capability_version"] != 1:
        raise SystemExit("%s: descriptor schema or capability_version changed; that is a new descriptor version"
                         % SOURCE)
    if descriptor["kernel"] != reference["kernel"]:
        raise SystemExit("%s: the kernel section is a contract, not inventory; it cannot differ" % SOURCE)

    supported, unsupported = descriptor["supported"], descriptor["unsupported"]
    for name, versions in supported.items():
        if (not isinstance(versions, list) or not versions or len(set(versions)) != len(versions)
                or any(type(v) is not int for v in versions)):
            raise SystemExit("%s: supported.%s must be a non-empty list of distinct integer versions"
                             % (SOURCE, name))
    if len(set(unsupported)) != len(unsupported):
        raise SystemExit("%s: unsupported lists a capability twice" % SOURCE)
    both = sorted(set(supported) & set(unsupported))
    if both:
        raise SystemExit("%s: %s both supported and unsupported" % (SOURCE, both))

    differences = set()
    if descriptor["implementation"] != reference["implementation"]:
        differences.add("implementation")
    for name in set(supported) | set(reference["supported"]):
        if supported.get(name) != reference["supported"].get(name):
            differences.add("supported." + name)
    if unsupported != reference["unsupported"]:
        differences.add("unsupported")
    mine, theirs = leaves(descriptor["coordinates"]), leaves(reference["coordinates"])
    if set(mine) != set(theirs):
        raise SystemExit("%s: the coordinate section's structure changed; that needs a coordinate version"
                         % SOURCE)
    for path in mine:
        if mine[path] != theirs[path]:
            differences.add("coordinates." + path)

    undocumented = sorted(differences - set(deviations))
    stale = sorted(set(deviations) - differences)
    if undocumented:
        raise SystemExit("%s: undocumented differences from the reference descriptor: %s" % (SOURCE, undocumented))
    if stale:
        raise SystemExit("%s: deviations names differences that no longer exist: %s" % (SOURCE, stale))
    for path in differences:
        if path.startswith("coordinates.") and not isinstance(mine[path[len("coordinates."):]], str):
            raise SystemExit("%s: %s changes a coordinate VALUE; only prose may differ" % (SOURCE, path))
    return descriptor


def render_capabilities():
    descriptor = load_descriptor()
    lines = [BANNER.format(source=SOURCE)]
    lines.append('#include "fwg/product_content.hpp"\n')
    lines.append("#include <string>\n")
    lines.append("namespace fwg {\n")
    lines.extend(text_function("capabilities_source_json", json_text(descriptor), "the capability descriptor"))
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {OUT / "content_capabilities.cpp": render_capabilities}
