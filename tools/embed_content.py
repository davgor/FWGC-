#!/usr/bin/env python3
"""Turn the content JSON under data/ into typed C++ tables under src/generated/.

    python tools/embed_content.py [--check]

Why generate rather than parse at runtime:

  * The library has no dependencies and no JSON *reader*, and adding one so that a
    thirteen-row table can be read at startup is a poor trade. Generated tables are
    typed, zero-cost and need no file to exist next to the binary -- which is what
    lets this drop into an engine build unchanged.
  * Content stays content. `data/*.json` is the source of truth, diffable and
    versioned, and nobody hand-edits a table in a .cpp.
  * The generated files are CHECKED IN, so building needs no Python.

`--check` regenerates into memory and fails if the checked-in output is stale. Run it
before committing a data change.

Validation here is deliberately fail-closed and mirrors the Python loader's: a registry
that has lost a row or a name is not a smaller catalogue, it is a broken one.
"""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
OUT = ROOT / "src" / "generated"

BANNER = """// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/{source}.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.
"""


def cpp_string(text):
    out = text.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + out.replace("\n", "\\n").replace("\t", "\\t") + '"'


def load_biomes():
    doc = json.loads((DATA / "biomes.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("biomes.json must declare schema version 1")

    rows = doc["natural_biomes"]
    if len({r["id"] for r in rows}) != len(rows):
        raise SystemExit("biomes.json has a duplicate natural biome id")

    allowed = {"id", "core", "color", "reachable", "unreachable_note"}
    for r in rows:
        extra = set(r) - allowed
        if extra:
            raise SystemExit("biomes.json row has unknown key(s): %s" % sorted(extra))
        if "reachable" in r and not isinstance(r["reachable"], bool):
            raise SystemExit("biomes.json reachable must be a boolean")
        if r.get("reachable") is False and not r.get("unreachable_note"):
            raise SystemExit("a tombstoned biome must say why it is unreachable")
        if len(r["color"]) != 3:
            raise SystemExit("biome %s needs an RGB triple" % r["id"])
    if not any(r.get("reachable", True) for r in rows):
        raise SystemExit("biomes.json tombstoned every natural biome")

    names = doc["variant_names"]
    lengths = {len(v) for v in names.values()}
    if lengths != {len(rows)}:
        raise SystemExit("every school needs one variant name per natural biome, in catalogue order")
    return rows, names


def display_name(row):
    # Matches the reference: id 17 is special-cased, everything else is the core name
    # with underscores opened out and sentence case applied.
    if row["id"] == 17:
        return "Persistent land ice"
    core = row["core"].replace("_", " ")
    return core[:1].upper() + core[1:].lower()


def render_biomes():
    rows, names = load_biomes()
    schools = sorted(names)

    lines = [BANNER.format(source="biomes.json")]
    lines.append('#include "fwg/content.hpp"\n')
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// ORDER IS A CONTRACT. `biome_variant` stores a flat index into the cross")
    lines.append("// product of these rows with the magic schools, so inserting a row renumbers")
    lines.append("// every variant in every world ever saved. Append, never insert.")
    lines.append("constexpr NaturalBiome kNaturalBiomes[] = {")
    for r in rows:
        lines.append("    {%d, %s, %s, %s, {%d, %d, %d}, %s},"
                     % (r["id"], cpp_string(r["core"]), cpp_string(display_name(r)),
                        cpp_string("terrain.biome.%03d" % r["id"]),
                        r["color"][0], r["color"][1], r["color"][2],
                        "true" if r.get("reachable", True) else "false"))
    lines.append("};\n")

    lines.append("// One variant name per natural biome, in catalogue order, per school.")
    lines.append("constexpr std::string_view kSchoolNames[] = {")
    for s in schools:
        lines.append("    %s," % cpp_string(s))
    lines.append("};\n")
    lines.append("constexpr std::string_view kVariantNames[][%d] = {" % len(rows))
    for s in schools:
        entries = ", ".join(cpp_string(v) for v in names[s])
        lines.append("    {%s}," % entries)
    lines.append("};\n")
    lines.append("}  // namespace\n")

    lines.append("std::span<const NaturalBiome> natural_biomes() { return kNaturalBiomes; }\n")
    lines.append("""const NaturalBiome* natural_biome(int id) {
    for (const NaturalBiome& row : kNaturalBiomes) {
        if (row.id == id) return &row;
    }
    return nullptr;
}
""")
    lines.append("std::span<const std::string_view> magic_schools() { return kSchoolNames; }\n")
    lines.append("""std::string_view variant_name(std::size_t school_index, std::size_t biome_index) {
    return kVariantNames[school_index][biome_index];
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def load_world_options():
    doc = json.loads((DATA / "world-options.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("world-options.json must declare schema version 1")
    opts = doc["options"]
    for name, spec in opts.items():
        if not name.replace("_", "").isalnum() or name[0].isdigit():
            raise SystemExit("option name %r is not a C++ identifier" % name)
        if spec["min"] > spec["max"]:
            raise SystemExit("option %s has min above max" % name)
        if not (spec["min"] <= spec["default"] <= spec["max"]):
            raise SystemExit("option %s default is outside its own bounds" % name)
        if spec["type"] not in ("number", "integer"):
            raise SystemExit("option %s has an unknown type %r" % (name, spec["type"]))
    return opts


def cpp_double(v):
    # Round-trip exactly, and keep it a double literal rather than an int.
    text = repr(float(v))
    return text if ("." in text or "e" in text or "E" in text or "inf" in text or "nan" in text) else text + ".0"


def render_world_options():
    opts = load_world_options()
    names = sorted(opts)

    lines = [BANNER.format(source="world-options.json")]
    lines.append('#include "fwg/options.hpp"\n')
    lines.append("#include <algorithm>\n")
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// Sorted by name so option_by_name can binary-search it.")
    lines.append("constexpr OptionSpec kOptions[] = {")
    for name in names:
        o = opts[name]
        lines.append('    {%s, %s, %s, %s, %s, %s, %s},'
                     % (cpp_string(name), cpp_double(o["default"]), cpp_double(o["min"]),
                        cpp_double(o["max"]), "true" if o["type"] == "integer" else "false",
                        cpp_string(o["units"]), cpp_string(o["group"])))
    lines.append("};\n")
    lines.append("static_assert(sizeof(kOptions) / sizeof(kOptions[0]) == kOptionCount,")
    lines.append('              "options.hpp enum and the generated table disagree");\n')
    lines.append("}  // namespace\n")
    lines.append("std::span<const OptionSpec> option_specs() { return kOptions; }\n")
    lines.append("""const OptionSpec& option_spec(Option option) {
    return kOptions[static_cast<std::size_t>(option)];
}
""")
    lines.append("""std::optional<Option> option_by_name(std::string_view name) {
    const auto* first = std::begin(kOptions);
    const auto* last = std::end(kOptions);
    const auto* it = std::lower_bound(first, last, name,
                                      [](const OptionSpec& spec, std::string_view key) { return spec.name < key; });
    if (it == last || it->name != name) return std::nullopt;
    return static_cast<Option>(it - first);
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def render_options_header():
    opts = load_world_options()
    names = sorted(opts)
    lines = [BANNER.format(source="world-options.json").replace("// GENERATED FILE", "// GENERATED ENUM")]
    lines.append("#pragma once\n")
    lines.append("namespace fwg {\n")
    lines.append("// One entry per world option, in the same order as the generated table.")
    lines.append("enum class Option : int {")
    for i, name in enumerate(names):
        lines.append("    %s = %d," % (name, i))
    lines.append("};\n")
    lines.append("inline constexpr int kOptionCount = %d;\n" % len(names))
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


def load_schools():
    doc = json.loads((DATA / "magic-schools.json").read_text(encoding="utf-8"))
    if doc.get("schema_version") != 1:
        raise SystemExit("magic-schools.json must declare schema version 1")
    rows = doc["schools"]
    names = [r["name"] for r in rows]
    if len(set(names)) != len(names):
        raise SystemExit("magic-schools.json has a duplicate school name")
    # Position is a contract, and hidden schools are appended. A hidden school before a
    # known one would renumber dominant_magic in every world ever saved.
    seen_hidden = False
    for r in rows:
        if r["hidden"]:
            seen_hidden = True
        elif seen_hidden:
            raise SystemExit("a known school follows a hidden one: hidden schools must be appended")
        if len(r["color"]) != 3:
            raise SystemExit("school %s needs an RGB triple" % r["name"])
    if not any(not r["hidden"] for r in rows):
        raise SystemExit("magic-schools.json hid every school")
    return rows


def render_schools():
    rows = load_schools()
    lines = [BANNER.format(source="magic-schools.json")]
    lines.append('#include "fwg/magic.hpp"\n')
    lines.append("namespace fwg {\nnamespace {\n")
    lines.append("// ORDER IS A CONTRACT. dominant_magic exports an index into this table.")
    lines.append("constexpr MagicSchool kSchools[] = {")
    for r in rows:
        lines.append("    {%s, %s, %s, {%d, %d, %d}, %s},"
                     % (cpp_string(r["name"]), cpp_string(r["group"]), cpp_string(r["descriptors"]),
                        r["color"][0], r["color"][1], r["color"][2],
                        "true" if r["hidden"] else "false"))
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::span<const MagicSchool> magic_school_table() { return kSchools; }\n")
    lines.append("""int magic_school_index(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kSchools); ++i) {
        if (kSchools[i].name == name) return static_cast<int>(i);
    }
    return -1;
}
""")
    lines.append("}  // namespace fwg")
    return "\n".join(lines) + "\n"


TARGETS = {
    OUT / "content_biomes.cpp": render_biomes,
    OUT / "content_schools.cpp": render_schools,
    OUT / "world_options.cpp": render_world_options,
    pathlib.Path(__file__).resolve().parent.parent / "include" / "fwg" / "option_ids.hpp": render_options_header,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if the checked-in output is stale")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    stale = []
    for path, render in TARGETS.items():
        text = render()
        if args.check:
            current = path.read_text(encoding="utf-8") if path.exists() else ""
            if current != text:
                stale.append(path.relative_to(ROOT).as_posix())
        else:
            path.write_text(text, encoding="utf-8")
            print("wrote", path.relative_to(ROOT).as_posix())

    if stale:
        print("STALE, regenerate with tools/embed_content.py: " + ", ".join(stale), file=sys.stderr)
        return 1
    if args.check:
        print("generated content is current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
