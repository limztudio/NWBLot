#!/usr/bin/env python3
"""Generate fixed distinct asset identities only for the optional benchmark's private cook root."""

import argparse
import hashlib
import json
from pathlib import Path

MATERIAL = '''material asset;

asset.interface = "project/shaders/smoke_surface.bind";
asset.surface = "project/shaders/gather_benchmark_glass.surface";
asset.bxdf = "project/shaders/refraction_smoke.bxdf";
asset.transparent = 1;
asset.two_sided = 0;
asset.refractive = 1;
asset.shader_variant = "default";
asset.parameters = {
    "surface": { "base_color": "half4(0.7h, 0.9h, 1.0h, 1.0h)" },
    "runtime": { "color_tint": "half4(1.0h, 1.0h, 1.0h, 1.0h)" },
};
'''


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def generated_contents(mesh_text, surface_text):
    result = {"shaders/gather_benchmark_glass.surface": crlf(surface_text)}
    for index in range(64):
        result[f"smoke/gather_benchmark/meshes/mesh_{index:02d}.nwb"] = crlf(mesh_text)
        result[f"smoke/gather_benchmark/materials/glass_{index:02d}.nwb"] = crlf(MATERIAL)
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
    # Material dispatch resolves one physical root per virtual project. Keep its existing bind/BXDF sources
    # and the generated surface in that same private root; two project roots would resolve the wrong include.
    contents = {path.relative_to(source_root).as_posix(): path.read_bytes()
        for path in sorted(source_root.rglob("*")) if path.is_file()}
    generated = generated_contents(sources[0].read_text(encoding="utf-8"), sources[1].read_text(encoding="utf-8"))
    if contents.keys() & generated.keys():
        parser.error("generated benchmark identities collide with source assets")
    contents.update(generated)
    if "generation_identity.json" in contents:
        parser.error("source assets collide with the reserved generation identity")
    expected = dict(contents)
    expected["generation_identity.json"] = (json.dumps({"schema": 1, "objects": 64,
        "templates": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
        "files": {relative: hashlib.sha256(payload).hexdigest() for relative, payload in contents.items()}}, indent=2) + "\n").encode("utf-8")
    existing = {path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()}
    unexpected = sorted(existing - expected.keys())
    if unexpected:
        parser.error("unexpected existing generated files: " + ", ".join(unexpected))
    for relative, payload in expected.items():
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    actual = {path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()}
    if actual != expected.keys():
        parser.error("generated output file identity changed while writing")
    mismatched = [relative for relative, payload in expected.items()
        if hashlib.sha256((output / relative).read_bytes()).digest() != hashlib.sha256(payload).digest()]
    if mismatched:
        parser.error("generated output payload verification failed: " + ", ".join(mismatched))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
