#!/usr/bin/env python3
"""Author a closed, outward two-joint glass prism for live CSG photon transitions."""

import argparse
from pathlib import Path

from generate_refraction_gallery_meshes import Mesh, serialize, validate


OUTPUT = Path(__file__).resolve().parent / "assets" / "characters" / "skinned_caustic_prism.nwb"


def build_prism():
    mesh = Mesh()
    mesh.positions = [(-.32, -.65, -.18), (.32, -.65, -.18), (.32, .65, -.18), (-.32, .65, -.18),
        (-.32, -.65, .18), (.32, -.65, .18), (.32, .65, .18), (-.32, .65, .18)]
    for face in ([0, 1, 2, 3], [4, 5, 6, 7], [0, 4, 5, 1], [3, 2, 6, 7], [0, 3, 7, 4], [1, 5, 6, 2]):
        mesh.flat_polygon(face, [(0., 0.), (1., 0.), (1., 1.), (0., 1.)])
    validate(mesh, 1, 2, ((-.32, -.65, -.18), (.32, .65, .18)))
    return mesh


def asset_text():
    mesh = build_prism()
    content = serialize(mesh).replace("generate_refraction_gallery_meshes.py", "generate_skinned_caustic_prism.py")
    content = content.replace("mesh asset;", "mesh mesh;").replace("asset.", "mesh.")
    identity = "[\n        [1, 0, 0, 0],\n        [0, 1, 0, 0],\n        [0, 0, 1, 0],\n    ]"
    lines = [content, "skeleton skeleton;", "", "skeleton.joints = [", "    {", '        "name": "root",',
        '        "local_bind_pose": ' + identity + ",", "    },", "    {", '        "name": "bend",',
        '        "parent": "root",', '        "local_bind_pose": ' + identity + ",", "    },", "];", "",
        "skin skin;", "", "skin.mesh = mesh;", "skin.skeleton = skeleton;", "", "skin.influences = ["]
    for _, y, _ in mesh.positions:
        # The lower four positions remain on root; the upper four follow the bending child.
        weights = (1, 0, 0, 0) if y < 0 else (0, 1, 0, 0)
        lines.append('    { "joints": [0, 1, 0, 0], "weights": [' + ", ".join(map(str, weights)) + "] },")
    lines += ["];", "", "skin.inverse_bind_matrices = [", "    " + identity + ",", "    " + identity + ",",
        "];", "", "model model;", "", "model.skeletons = {", '    "skeleton": { "skeleton": skeleton, },',
        "};", "", "model.skinned_meshes = {", '    "mesh": { "skin": skin, "skeleton": "skeleton", },',
        "};", "", "asset_bunch bunch = [mesh, skeleton, skin, model];", ""]
    return "\n".join(lines).replace("\n", "\r\n").encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = asset_text()
    if args.check:
        assert OUTPUT.read_bytes() == content, "Stale authored skinned caustic prism"
    else:
        OUTPUT.write_bytes(content)
    print("Skinned caustic prism: 12 outward triangles, one closed component, Euler 2, two joints, eight normalized influences")


if __name__ == "__main__":
    main()
