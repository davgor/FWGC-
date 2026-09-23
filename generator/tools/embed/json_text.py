"""Emit a JSON document into C++ as chunked raw string literals. A helper, not a dataset.

The product package's documents -- the capability descriptor, the taxonomy, the guild
policies, the control catalogue and the asset list's sources -- are published AS JSON, with
free-form nested metadata copied verbatim from authored rows. A typed table would have to
name every one of those fields and would still lose what the published bytes depend on:
key order, and the difference between 1 and 1.0. So these are embedded as the JSON text
itself, validated here, and read once at runtime by fwg::parse_json.

The text is written with ensure_ascii, so the generated source is pure ASCII, and split into
pieces well under MSVC's 16380-byte limit on one string literal; the library joins them.
No module here declares TARGETS, so embed_content.py discovers nothing to render in it.
"""
import json

CHUNK = 8000
DELIMITER = "fwgjson"


def json_text(value, sort_keys=False):
    """Compact, ASCII, and exactly what CPython writes for these values."""
    return json.dumps(value, ensure_ascii=True, sort_keys=sort_keys, separators=(",", ":"), allow_nan=False)


def text_function(name, text, what):
    """C++ for `std::string_view name()` returning `text`, built from literal pieces."""
    closing = ")" + DELIMITER + '"'
    if closing in text:
        raise SystemExit("%s contains the raw-literal terminator %r" % (what, closing))
    if any(ord(c) > 0x7E or ord(c) < 0x20 for c in text):
        raise SystemExit("%s is not printable ASCII; emit it with ensure_ascii" % what)
    pieces = [text[i:i + CHUNK] for i in range(0, len(text), CHUNK)] or [""]
    lines = ["namespace {\n",
             "// %s: %d bytes of JSON in %d pieces." % (what, len(text), len(pieces)),
             "constexpr std::string_view k_%s[] = {" % name]
    for piece in pieces:
        lines.append('    R"%s(%s)%s",' % (DELIMITER, piece, DELIMITER))
    lines.append("};\n")
    lines.append("}  // namespace\n")
    lines.append("std::string_view %s() {" % name)
    lines.append("    static const std::string joined = [] {")
    lines.append("        std::string text;")
    lines.append("        text.reserve(%d);" % len(text))
    lines.append("        for (const std::string_view piece : k_%s) text.append(piece);" % name)
    lines.append("        return text;")
    lines.append("    }();")
    lines.append("    return joined;")
    lines.append("}\n")
    return lines
