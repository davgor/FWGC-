#!/usr/bin/env python3
"""Render a world document as an ASCII map, for looking at a planet without an engine.

    python tools/preview.py world.json [--cols 96] [--rows 44] [--layer height]

The height ramp is built from PERCENTILES of the land cells, not from the range. A
tectonic world can carry a single 20 km peak, and a linear ramp against the maximum
flattens every continent to blank space -- which looks exactly like a generator that
produced no land, and cost an hour the first time it happened.
"""
import argparse
import json
import sys

RAMP = ".:-=+*#@"


def render(doc, cols, rows, layer):
    grid = doc["layers"][layer]
    n = len(grid)
    sea = doc["effective_config"]["sea_level"] if layer == "height" else 0.0

    values = [v for row in grid for v in row[:-1]]
    above = sorted(v for v in values if v > sea)
    if not above:
        return f"no cell of '{layer}' is above {sea}"

    stops = [above[min(len(above) - 1, int(p * len(above)))]
             for p in (0.20, 0.45, 0.65, 0.80, 0.90, 0.96, 0.99)]

    lines = []
    for r in range(rows):
        z = int(r * (n - 1) / (rows - 1))
        line = []
        for c in range(cols):
            x = int(c * (n - 2) / (cols - 1))
            v = grid[z][x]
            if v <= sea:
                line.append(" " if v < sea - 300 else "~")
            else:
                line.append(RAMP[sum(1 for s in stops if v >= s)])
        lines.append("".join(line))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("world")
    parser.add_argument("--cols", type=int, default=96)
    parser.add_argument("--rows", type=int, default=44)
    parser.add_argument("--layer", default="height")
    args = parser.parse_args()

    with open(args.world, encoding="utf-8") as handle:
        doc = json.load(handle)

    if args.layer not in doc["layers"]:
        sys.exit(f"no layer '{args.layer}'; this world has: {', '.join(sorted(doc['layers']))}")

    print(render(doc, args.cols, args.rows, args.layer))

    cfg = doc["config"]
    height = [v for row in doc["layers"]["height"] for v in row[:-1]]
    area = doc.get("area", {})
    print(f"\nseed {cfg['seed']}  size {cfg['size']}  stage {doc['completed_stage']}/16  "
          f"land {area.get('land_fraction', 0) * 100:.1f}%  "
          f"peak {max(height):.0f} m  deepest {min(height):.0f} m  "
          f"octaves {doc['resolved_octaves']}/{cfg['octaves']}")
    for warning in doc.get("warnings", []):
        print(f"  note: {warning}")


if __name__ == "__main__":
    main()
