#!/usr/bin/env python3
"""Generate fixed distinct asset identities only for the optional benchmark's private cook root."""

import argparse
import hashlib
import json
from pathlib import Path

LIT_R_N = "\r\n"
LIT_N = "\n"
LIT_UTF_8 = "utf-8"
LIT_GENERATION_IDENTITY_JSON = "generation_identity.json"
LIT_EMPTY = ", "
LIT_MAIN = "__main__"

PROJECT = """project asset;

asset.start_scene = "project/scenes/main";
"""

MATERIAL = """material asset;

asset.interface = "project/shaders/smoke_surface.bind";
asset.surface = "project/shaders/gather_benchmark_glass.surface";
asset.bxdf = "project/shaders/refraction_smoke.bxdf";
asset.transparent = 1;
asset.two_sided = 0;
asset.refractive = 1;
asset.shader_variant = "default";
asset.parameters = {
    "surface": {
        "base_color_map": "project/textures/smoke_checker",
        "base_color_sampler": "engine/samplers/linear_clamp",
        "base_color": "half4(0.7h, 0.9h, 1.0h, 1.0h)",
    },
    "runtime": { "color_tint": "half4(1.0h, 1.0h, 1.0h, 1.0h)" },
};
"""


def crlf(text):
    return text.replace(LIT_R_N, LIT_N).replace(LIT_N, LIT_R_N).encode(LIT_UTF_8)


def generated_contents(mesh_text, surface_text):
    result = {
        "project.nwb": crlf(PROJECT),
        "shaders/gather_benchmark_glass.surface": crlf(surface_text),
    }
    scene_assets = [
        "project/characters/body/model",
        "project/meshes/octahedron",
        "project/meshes/shadow_plane",
        "project/smoke/reflection/materials/opaque",
    ]
    for index in range(64):
        mesh_rel = f"smoke/gather_benchmark/meshes/mesh_{index:02d}.nwb"
        mat_rel = f"smoke/gather_benchmark/materials/glass_{index:02d}.nwb"
        result[mesh_rel] = crlf(mesh_text)
        result[mat_rel] = crlf(MATERIAL)
        scene_assets.append("project/" + mesh_rel[:-4])
        scene_assets.append("project/" + mat_rel[:-4])
    assets_lines = ("," + LIT_N).join(f'    "asset_{idx:03d}": "{item}"' for idx, item in enumerate(sorted(scene_assets)))
    scene_text = (
        "scene asset;\n\n"
        'asset.subsystems = [\n    "engine/subsystems/renderer",\n    "engine/subsystems/mesh",\n    "engine/subsystems/deferred",\n    "engine/subsystems/bvh",\n    "engine/subsystems/shadow",\n    "engine/subsystems/gi",\n    "engine/subsystems/reflection",\n    "engine/subsystems/skinning",\n    "engine/subsystems/transparent",\n    "engine/subsystems/refractive"\n];\n\n'
        f"asset.assets = {{\n{assets_lines}\n}};\n"
    )
    result["scenes/main.nwb"] = crlf(scene_text)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--mesh-template", type=Path, required=True)
    parser.add_argument("--surface-template", type=Path, required=True)
    args = parser.parse_args(argv)
    output = args.output_root.resolve()
    source_root = args.source_root.resolve()
    sources = (args.mesh_template.resolve(), args.surface_template.resolve())
    if not source_root.is_dir():
        parser.error("source root must be an existing directory")
    if output == Path(output.anchor) or source_root.is_relative_to(output) or output.is_relative_to(source_root) \
        or any(path.is_relative_to(output) for path in sources):
        parser.error("generated root must be separate from its authored templates")
    # Keep bind, BXDF, and generated surface files in one private project root.
    contents = {path.relative_to(source_root).as_posix(): path.read_bytes() for path in sorted(source_root.rglob("*")) if path.is_file()}
    generated = generated_contents(sources[0].read_text(encoding=LIT_UTF_8), sources[1].read_text(encoding=LIT_UTF_8))
    if contents.keys() & generated.keys():
        parser.error("generated benchmark identities collide with source assets")
    contents.update(generated)
    if LIT_GENERATION_IDENTITY_JSON in contents:
        parser.error("source assets collide with the reserved generation identity")
    expected = dict(contents)
    expected[LIT_GENERATION_IDENTITY_JSON] = (json.dumps({"schema": 1, "objects": 64, "templates": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
        "files": {relative: hashlib.sha256(payload).hexdigest() for relative, payload in contents.items()}}, indent=2) + LIT_N).encode(LIT_UTF_8)
    existing = {path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()}
    unexpected = sorted(existing - expected.keys())
    if unexpected:
        parser.error("unexpected existing generated files: " + LIT_EMPTY.join(unexpected))
    for relative, payload in expected.items():
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    actual = {path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()}
    if actual != expected.keys():
        parser.error("generated output file identity changed while writing")
    mismatched = [relative for relative, payload in expected.items() if hashlib.sha256((output / relative).read_bytes()).digest() != hashlib.sha256(payload).digest()]
    if mismatched:
        parser.error("generated output payload verification failed: " + LIT_EMPTY.join(mismatched))
    return 0


if __name__ == LIT_MAIN:
    raise SystemExit(main())
