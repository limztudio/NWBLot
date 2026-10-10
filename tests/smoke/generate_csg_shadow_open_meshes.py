#!/usr/bin/env python3
"""Author disconnected open quad components for the CSG shadow capacity boundary."""

import argparse
from collections import Counter
from pathlib import Path

from generate_refraction_gallery_meshes import Mesh, serialize


ROOT = Path(__file__).resolve().parent / "assets" / "meshes"
SHEET_COUNTS = (2, 5, 6, 64, 65)


def open_sheets(count):
    mesh = Mesh()
    for sheet in range(count):
        z = -.5 + sheet / (count - 1)
        base = len(mesh.positions)
        mesh.positions.extend((x, y, z) for x, y in ((-.5, -.5), (.5, -.5), (.5, .5), (-.5, .5)))
        mesh.flat_polygon(list(range(base, base + 4)), [(0., 0.), (1., 0.), (1., 1.), (0., 1.)])
    validate_open_sheets(mesh, count)
    return mesh


def validate_open_sheets(mesh, count):
    if len(mesh.positions) != 4 * count or len(mesh.indices) != 2 * count:
        raise AssertionError("open-sheet primitive or position count changed")
    for sheet in range(count):
        indices = [tuple(mesh.vertex_refs[index][0] for index in triangle)
            for triangle in mesh.indices[2 * sheet:2 * sheet + 2]]
        if set(index for triangle in indices for index in triangle) != set(range(4 * sheet, 4 * sheet + 4)):
            raise AssertionError("open sheets became connected or lost a corner")
        edges = Counter(tuple(sorted((a, b))) for triangle in indices for a, b in zip(triangle, triangle[1:] + triangle[:1]))
        if sorted(edges.values()) != [1, 1, 1, 1, 2]:
            raise AssertionError("sheet no longer has four unmatched perimeter edges")
        depths = {mesh.positions[index][2] for triangle in indices for index in triangle}
        if depths != {-.5 + sheet / (count - 1)}:
            raise AssertionError("sheet is not planar at its independent analytic depth")
    if 2. / (count - 1) <= 1e-3:
        raise AssertionError("world sheet spacing approaches the production endpoint merge tolerance")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    for count in SHEET_COUNTS:
        mesh = open_sheets(count)
        content = serialize(mesh).replace("generate_refraction_gallery_meshes.py", "generate_csg_shadow_open_meshes.py")
        encoded = content.replace("\n", "\r\n").encode("utf-8")
        path = ROOT / f"csg_shadow_open_{count}.nwb"
        if args.check:
            if path.read_bytes() != encoded:
                raise AssertionError(f"stale generated open-sheet asset: {path}")
        else:
            path.write_bytes(encoded)
        print(f"{path.name}: {count} open components, {2 * count} triangles, no closed component")


if __name__ == "__main__":
    main()
