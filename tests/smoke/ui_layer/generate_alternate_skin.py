#!/usr/bin/env python3
"""Generate the isolated amber/green replacement skin using engine artwork primitives."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

LIT_ATLAS_NWB = "atlas.nwb"
LIT_UTF_8 = "utf-8"
LIT_NAME = "name"
LIT_PADDING = "padding"
LIT_ASSET_REGIONS_OPEN = "asset.regions = ["

REPO = Path(__file__).resolve().parents[3]
OUTPUT = Path(__file__).resolve().parent / "assets/ui/skins/alternate"
PALETTE = { "panel.normal": ((39, 63, 34), (116, 150, 79)), "window.normal": ((45, 72, 38), (132, 162, 87)), "window.title": ((142, 92, 28), (214, 169, 76)), "button.normal": ((49, 100, 63), (111, 168, 105)), "button.hover": ((69, 135, 77), (162, 204, 110)),
    "button.pressed": ((36, 76, 48), (162, 191, 91)), "button.disabled": ((54, 60, 42), (95, 101, 67)), "checkbox.normal": ((38, 65, 36), (129, 157, 80)), "checkbox.hover": ((58, 87, 44), (176, 195, 95)), "checkbox.checked": ((157, 111, 30), (229, 195, 89)),
    "checkbox.disabled": ((54, 60, 42), (95, 101, 67)), "focus.overlay": (None, (226, 186, 81)), "separator": ((135, 156, 73), (135, 156, 73)), }


def generate(work_directory, output_directory, converter):
    spec = importlib.util.spec_from_file_location("engine_skin_artwork", REPO / "utilities/ui_skin/generate_default.py")
    artwork = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(artwork)
    # Reverse tile placement as well as replacing colors, so fixed default UV assumptions fail visibly.
    artwork.PANELS = [(name, *PALETTE.get(name, (fill, border))) for name, fill, border in reversed(artwork.PANELS)]
    artwork.ICONS = list(reversed(artwork.ICONS))
    artwork.generate(work_directory)
    metadata = (work_directory / LIT_ATLAS_NWB).read_text(encoding=LIT_UTF_8)
    region_lines = metadata.split(LIT_ASSET_REGIONS_OPEN, 1)[1].split("];", 1)[0].splitlines()
    regions = [json.loads(line.strip().rstrip(",")) for line in region_lines if line.strip().startswith("{")]
    expected_regions = len(artwork.PANELS) + len(artwork.ICONS) + 10  # Default artwork plus semantic aliases.
    if (len(regions) != expected_regions or len({region[LIT_NAME] for region in regions}) != len(regions) or any("rect" not in region for region in regions)):
        raise ValueError("default skin region catalog changed unexpectedly")
    for region in regions:
        if region[LIT_NAME] == "window.normal":
            region[LIT_PADDING] = [6.0, 7.0, 10.0, 5.0]
        elif region[LIT_NAME] == "window.title":
            region[LIT_PADDING] = [10.0, 4.0, 6.0, 8.0]
    colors = [dict(color) for color in artwork.COLOR_ROLES]
    for color in colors:
        if color[LIT_NAME] == "text.tooltip":
            color["rgba"] = [1.0, 0.58, 0.26, 1.0]
    lines = ["ui_skin asset;", "", 'asset.texture = "project/ui/skins/alternate/texture";', "asset.atlas_extent = [256, 256];", "asset.reference_density = 1.0;", "asset.toolkit_contract = \"widgets\";", LIT_ASSET_REGIONS_OPEN]
    lines += ["    " + json.dumps(region, separators=(", ", ": ")) + "," for region in regions]
    lines += ["];", "asset.colors = ["]
    lines += ["    " + json.dumps(color, separators=(", ", ": ")) + "," for color in colors]
    lines += ["];", 'asset.typography = {"default_font_size": 16.0};', ""]
    output_directory.mkdir(parents=True, exist_ok=True)
    (output_directory / LIT_ATLAS_NWB).write_bytes("\r\n".join(lines).encode(LIT_UTF_8))
    subprocess.run([str(converter.resolve()), str(work_directory / "source.png"), "--output", str(output_directory / "texture"), "--force"], check=True)
    print(f"Replacement skin: {len(regions)} remapped regions, amber title, green frame; {output_directory}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tex-conv", type=Path, default=REPO / "__exec/windows/arm64/full/opt/tex_conv.exe")
    parser.add_argument("--work-directory", type=Path, default=REPO / "__artifacts/custom_ui/alternate_skin")
    parser.add_argument("--output-directory", type=Path, default=OUTPUT)
    args = parser.parse_args()
    if not args.tex_conv.is_file():
        parser.error("--tex-conv must name a built converter executable")
    generate(args.work_directory.resolve(), args.output_directory.resolve(), args.tex_conv)


if __name__ == "__main__":
    main()
