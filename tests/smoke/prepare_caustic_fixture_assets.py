#!/usr/bin/env python3
"""Prepare the private skinning catalog with a closed glass-body variant."""

import argparse
import ast
from collections import defaultdict
import math
from pathlib import Path
import struct
import tempfile

from prepare_fixture_asset_root import prepare_asset_root


def read_array(text, key):
    start = text.index(key + " = [") + len(key + " = ")
    depth = 0
    for end in range(start, len(text)):
        if text[end] == "[":
            depth += 1
        elif text[end] == "]":
            depth -= 1
            if depth == 0:
                return start, end + 1, ast.literal_eval(text[start:end + 1])
    raise ValueError(f"unterminated source array: {key}")


def subtract(left, right):
    return tuple(a - b for a, b in zip(left, right))


def cross(left, right):
    return (left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0])


def select_closed_triangles(positions, references, indices):
    positions = [tuple(struct.unpack("f", struct.pack("f", value))[0] for value in row) for row in positions]
    if any(len(row) != 3 or not all(math.isfinite(value) for value in row) for row in positions):
        raise ValueError("source positions must be finite three-component vectors")
    triangles = []
    for row in indices:
        if len(row) != 3 or any(not isinstance(index, int) or index < 0 or index >= len(references) for index in row):
            raise ValueError("source triangle references are invalid")
        triangle = tuple(references[index][0] for index in row)
        if any(not isinstance(index, int) or index < 0 or index >= len(positions) for index in triangle):
            raise ValueError("source position references are invalid")
        triangles.append(triangle)

    parents = list(range(len(triangles)))

    def find(index):
        while parents[index] != index:
            parents[index] = parents[parents[index]]
            index = parents[index]
        return index

    edges = defaultdict(list)
    degenerate = set()
    for index, triangle in enumerate(triangles):
        a, b, c = (positions[vertex] for vertex in triangle)
        if len(set(triangle)) != 3 or cross(subtract(b, a), subtract(c, a)) == (0.0, 0.0, 0.0):
            degenerate.add(index)
        for first, second in zip(triangle, triangle[1:] + triangle[:1]):
            edges[min(first, second), max(first, second)].append((index, first < second))
    for entries in edges.values():
        root = find(entries[0][0])
        for index, _ in entries:
            parents[find(index)] = root

    components = defaultdict(list)
    for index in range(len(triangles)):
        components[find(index)].append(index)
    invalid = {find(index) for index in degenerate}
    for entries in edges.values():
        if len(entries) != 2 or entries[0][1] == entries[1][1]:
            invalid.update(find(index) for index, _ in entries)

    retained = set()
    closed_components = 0
    for root, rows in components.items():
        if root in invalid:
            continue
        origin = positions[triangles[rows[0]][0]]
        volumes = []
        for index in rows:
            a, b, c = (subtract(positions[vertex], origin) for vertex in triangles[index])
            normal = cross(b, c)
            volumes.append(sum(value * direction for value, direction in zip(a, normal)))
        volume = math.fsum(volumes)
        if not math.isfinite(volume) or volume <= 0.0:
            raise ValueError("closed source component must have finite positive signed volume")
        retained.update(rows)
        closed_components += 1
    if not retained:
        raise ValueError("source contains no outward closed components")
    return [row for index, row in enumerate(indices) if index in retained], closed_components


def prepare_caustic_assets(source_root, output_root):
    source = source_root.resolve()
    body = source / "characters/body.nwb"
    payload = body.read_bytes()
    text = payload.decode("utf-8")
    _, _, positions = read_array(text, "mesh.positions")
    _, _, references = read_array(text, "mesh.vertex_refs")
    start, end, indices = read_array(text, "mesh.indices")
    selected, components = select_closed_triangles(positions, references, indices)
    newline = "\r\n" if "\r\n" in text else "\n"
    replacement = "[" + newline + "".join(
        "    [" + ", ".join(str(value) for value in row) + "]," + newline for row in selected
    ) + "]"
    variant = (text[:start] + replacement + text[end:]).encode("utf-8")
    with tempfile.TemporaryDirectory(prefix="nwb_caustic_fixture_") as temporary:
        generated = Path(temporary)
        variant_path = generated / "characters/caustic_closed_body.nwb"
        variant_path.parent.mkdir(parents=True)
        variant_path.write_bytes(variant)
        count = prepare_asset_root(output_root, [source, generated])
    if body.read_bytes() != payload:
        raise ValueError("authored body changed while preparing the private catalog")
    return count, len(indices), len(selected), components


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        count, original, retained, components = prepare_caustic_assets(args.source_root, args.output_root)
    except (OSError, ValueError, IndexError, TypeError, SyntaxError, OverflowError) as error:
        parser.error(str(error))
    print(f"Prepared {count} fixture assets; closed glass body retains {retained}/{original} triangles in {components} components")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
