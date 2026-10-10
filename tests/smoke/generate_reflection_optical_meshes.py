#!/usr/bin/env python3
"""Author closed planar optical smoke meshes; --check verifies their exact checked-in data."""

import argparse
from pathlib import Path

from generate_refraction_gallery_meshes import Mesh, serialize, validate

LIT_MAIN = "__main__"
LIT_UTF_8 = "utf-8"
LIT_STORE_TRUE = "store_true"


ROOT = Path(__file__).resolve().parent / "assets" / "meshes"


def append_box(mesh, minimum_z, maximum_z):
    assert minimum_z < maximum_z
    center_z = (minimum_z + maximum_z) * .5
    half_thickness = (maximum_z - minimum_z) * .5
    local = Mesh()
    local.positions = [(-12., -9., -half_thickness), (12., -9., -half_thickness),
        (12., 9., -half_thickness), (-12., 9., -half_thickness),
        (-12., -9., half_thickness), (12., -9., half_thickness),
        (12., 9., half_thickness), (-12., 9., half_thickness)]
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
    append_box(mesh, -1.25, -.75)
    append_box(mesh, .75, 1.25)
    return mesh


def overlapping_boxes():
    mesh = Mesh()
    append_box(mesh, -.4, .1)
    append_box(mesh, -.1, .4)
    return mesh


def grouped_gap_boxes(first_gap, second_gap):
    assert 0. < first_gap < second_gap
    # The near-coincident exit/entries take winding 1 -> 0 -> 1 -> 2 despite their positive net winding delta.
    mesh = Mesh()
    append_box(mesh, 0., 1.)
    append_box(mesh, -1., -first_gap)
    append_box(mesh, -.5, -second_gap)
    return mesh


def grouped_entry_boxes(second_gap):
    # A bounded cutter may replace the final entry of a changed-membership group while leaving the preceding thin solid intact.
    mesh = Mesh()
    append_box(mesh, 0., 2e-6)
    append_box(mesh, -1., -second_gap)
    return mesh


def tir_prism():
    mesh = Mesh()
    mesh.positions = [(x, y, z) for y in (-10., 10.) for x, z in ((-10., 2.), (20., 2.), (20., -28.))]
    for face in ([0, 1, 2], [3, 4, 5], [0, 3, 4, 1], [1, 4, 5, 2], [2, 5, 3, 0]):
        mesh.flat_polygon(face, [(float(index % 2), float(index // 2)) for index in range(len(face))])
    return mesh


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action=LIT_STORE_TRUE)
    args = parser.parse_args()
    for filename, mesh, components, euler, bounds in (
        ("reflection_disconnected_boxes.nwb", disconnected_boxes(), 2, 4, ((-12., -9., -1.25), (12., 9., 1.25))),
        ("reflection_overlapping_boxes.nwb", overlapping_boxes(), 2, 4, ((-12., -9., -.4), (12., 9., .4))),
        ("reflection_tir_prism.nwb", tir_prism(), 1, 2, ((-10., -10., -28.), (20., 10., 2.))),
        ("reflection_group_gap.nwb", grouped_gap_boxes(4e-6, 6e-6), 3, 6, ((-12., -9., -1.), (12., 9., 1.))),
        ("reflection_group_gap_sub_ulp.nwb", grouped_gap_boxes(8e-8, 1e-7), 3, 6, ((-12., -9., -1.), (12., 9., 1.))),
        ("reflection_group_entry.nwb", grouped_entry_boxes(12e-6), 2, 4, ((-12., -9., -1.), (12., 9., 2e-6))),
        ("reflection_csg_group_entry.nwb", grouped_entry_boxes(6e-6), 2, 4, ((-12., -9., -1.), (12., 9., 2e-6))),
    ):
        validate(mesh, components, euler, bounds)
        content = serialize(mesh).replace("generate_refraction_gallery_meshes.py", "generate_reflection_optical_meshes.py")
        encoded = content.replace("\n", "\r\n").encode(LIT_UTF_8)
        path = ROOT / filename
        if args.check:
            if path.read_bytes() != encoded:
                raise AssertionError(f"stale generated optical mesh: {path}")
        else:
            path.write_bytes(encoded)
        print(f"{filename}: {len(mesh.indices)} triangles, {components} closed outward components")


if __name__ == LIT_MAIN:
    main()
