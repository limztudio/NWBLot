#!/usr/bin/env python3
"""Author closed planar optical smoke meshes; --check verifies their exact checked-in data."""

import argparse
from pathlib import Path

from generate_refraction_gallery_meshes import Mesh, serialize, validate


ROOT = Path(__file__).resolve().parent / "assets" / "meshes"


def append_box(mesh, center_z):
    local = Mesh()
    local.positions = [(-12., -9., -.25), (12., -9., -.25), (12., 9., -.25), (-12., 9., -.25),
        (-12., -9., .25), (12., -9., .25), (12., 9., .25), (-12., 9., .25)]
    for face in ([0, 1, 2, 3], [4, 5, 6, 7], [0, 4, 5, 1], [3, 2, 6, 7], [0, 3, 7, 4], [1, 5, 6, 2]):
        local.flat_polygon(face, [(0., 0.), (1., 0.), (1., 1.), (0., 1.)])
    bases = len(mesh.positions), len(mesh.normals), len(mesh.tangents), len(mesh.uv0), len(mesh.vertex_refs)
    mesh.positions.extend((x, y, z + center_z) for x, y, z in local.positions)
    mesh.normals.extend(local.normals)
    mesh.tangents.extend(local.tangents)
    mesh.uv0.extend(local.uv0)
    mesh.vertex_refs.extend(tuple(value + bases[index] if index < 4 else value for index, value in enumerate(ref))
        for ref in local.vertex_refs)
    mesh.indices.extend(tuple(index + bases[4] for index in triangle) for triangle in local.indices)


def disconnected_boxes():
    mesh = Mesh()
    append_box(mesh, -1.)
    append_box(mesh, 1.)
    return mesh


def overlapping_boxes():
    mesh = Mesh()
    append_box(mesh, -.15)
    append_box(mesh, .15)
    return mesh


def tir_prism():
    mesh = Mesh()
    mesh.positions = [(x, y, z) for y in (-10., 10.) for x, z in ((-10., 2.), (20., 2.), (20., -28.))]
    for face in ([0, 1, 2], [3, 4, 5], [0, 3, 4, 1], [1, 4, 5, 2], [2, 5, 3, 0]):
        mesh.flat_polygon(face, [(float(index % 2), float(index // 2)) for index in range(len(face))])
    return mesh


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for filename, mesh, components, euler, bounds in (
        ("reflection_disconnected_boxes.nwb", disconnected_boxes(), 2, 4, ((-12., -9., -1.25), (12., 9., 1.25))),
        ("reflection_overlapping_boxes.nwb", overlapping_boxes(), 2, 4, ((-12., -9., -.4), (12., 9., .4))),
        ("reflection_tir_prism.nwb", tir_prism(), 1, 2, ((-10., -10., -28.), (20., 10., 2.))),
    ):
        validate(mesh, components, euler, bounds)
        content = serialize(mesh).replace("generate_refraction_gallery_meshes.py", "generate_reflection_optical_meshes.py")
        encoded = content.replace("\n", "\r\n").encode("utf-8")
        path = ROOT / filename
        if args.check:
            if path.read_bytes() != encoded:
                raise AssertionError(f"stale generated optical mesh: {path}")
        else:
            path.write_bytes(encoded)
        print(f"{filename}: {len(mesh.indices)} triangles, {components} closed outward components")


if __name__ == "__main__":
    main()
