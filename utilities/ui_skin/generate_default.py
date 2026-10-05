#!/usr/bin/env python3
"""Generate the engine's default UI artwork and MetaScript atlas deterministically."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
from typing import Optional, Sequence
import zlib


REPO = Path(__file__).resolve().parents[2]
DEFAULT_DIRECTORY = REPO / "impl/assets/ui/skins/default"
ATLAS_SIZE = 256
TILE_SIZE = 32
REGION_SIZE = 24
GUTTER = 4

# Name, fill RGB, border RGB; RGB is authored in sRGB and alpha is straight coverage.
PANELS = [
    ("panel.normal", (24, 29, 37), (63, 77, 93)),
    ("window.normal", (27, 34, 44), (71, 86, 105)),
    ("window.title", (34, 47, 64), (71, 86, 105)),
    ("button.normal", (45, 60, 80), (79, 101, 126)),
    ("button.hover", (60, 84, 112), (107, 147, 188)),
    ("button.pressed", (33, 47, 65), (88, 128, 171)),
    ("button.disabled", (37, 43, 52), (58, 65, 75)),
    ("edit.normal", (15, 20, 28), (65, 80, 100)),
    ("edit.focused", (18, 26, 37), (97, 174, 240)),
    ("edit.disabled", (29, 33, 40), (54, 61, 70)),
    ("checkbox.normal", (20, 27, 36), (81, 103, 129)),
    ("checkbox.hover", (34, 48, 65), (109, 163, 211)),
    ("checkbox.checked", (40, 102, 164), (110, 186, 245)),
    ("checkbox.disabled", (32, 37, 45), (57, 65, 76)),
    ("radio.normal", (20, 27, 36), (81, 103, 129)),
    ("radio.checked", (40, 102, 164), (110, 186, 245)),
    ("list.normal", (17, 23, 31), (62, 77, 95)),
    ("list.row.hover", (39, 58, 80), (39, 58, 80)),
    ("list.row.selected", (38, 80, 123), (59, 113, 168)),
    ("popup.normal", (26, 34, 45), (81, 101, 126)),
    ("tooltip.normal", (39, 48, 61), (95, 112, 133)),
    ("scrollbar.track", (17, 23, 31), (40, 50, 63)),
    ("scrollbar.thumb.normal", (65, 82, 103), (82, 104, 130)),
    ("scrollbar.thumb.hover", (88, 119, 151), (116, 153, 190)),
    ("slider.track", (20, 29, 41), (63, 80, 101)),
    ("slider.thumb.normal", (69, 131, 185), (130, 193, 238)),
    ("slider.thumb.hover", (86, 164, 221), (166, 221, 255)),
    ("progress.track", (20, 29, 41), (63, 80, 101)),
    ("progress.fill", (53, 131, 192), (97, 176, 233)),
    ("focus.overlay", None, (124, 194, 251)),
    ("separator", (65, 80, 99), (65, 80, 99)),
]
ICONS = [
    ("checkbox.mark", "check"),
    ("radio.mark", "dot"),
    ("combo.arrow", "down"),
    ("window.close", "cross"),
    ("window.collapse", "down"),
    ("scrollbar.arrow.up", "up"),
    ("scrollbar.arrow.down", "down"),
    ("scrollbar.arrow.left", "left"),
    ("scrollbar.arrow.right", "right"),
    ("white", "white"),
]

# Linear RGB and straight alpha, in UiSkinColorRole order.
COLOR_ROLES = [
    {"name": "text.normal", "rgba": [0.92, 0.94, 0.98, 1.0]},
    {"name": "text.disabled", "rgba": [0.48, 0.50, 0.55, 1.0]},
    {"name": "text.tooltip", "rgba": [1.0, 1.0, 1.0, 1.0]},
    {"name": "edit.background", "rgba": [0.08, 0.10, 0.14, 1.0]},
    {"name": "edit.selection", "rgba": [0.20, 0.40, 0.78, 0.75]},
    {"name": "edit.inactive_selection", "rgba": [0.28, 0.31, 0.38, 0.55]},
    {"name": "edit.caret", "rgba": [0.95, 0.97, 1.0, 1.0]},
    {"name": "edit.preedit", "rgba": [0.50, 0.72, 1.0, 1.0]},
    {"name": "scrollbar.track", "rgba": [0.08, 0.10, 0.14, 1.0]},
    {"name": "scrollbar.thumb", "rgba": [0.35, 0.40, 0.48, 1.0]},
    {"name": "scrollbar.disabled", "rgba": [0.25, 0.28, 0.32, 1.0]},
    {"name": "popup.backdrop", "rgba": [0.0, 0.0, 0.0, 0.4]},
    {"name": "control.hover_tint", "rgba": [1.08, 1.08, 1.08, 1.0]},
    {"name": "control.pressed_tint", "rgba": [0.85, 0.85, 0.85, 1.0]},
    {"name": "control.disabled_tint", "rgba": [0.55, 0.55, 0.55, 0.6]},
    {"name": "progress.track_tint", "rgba": [1.0, 1.0, 1.0, 1.0]},
    {"name": "progress.fill_tint", "rgba": [1.0, 1.0, 1.0, 1.0]},
]


def rounded_distance(x: float, y: float, radius: float = 5.0) -> float:
    half = REGION_SIZE / 2
    dx = abs(x - half) - (half - radius)
    dy = abs(y - half) - (half - radius)
    return math.hypot(max(dx, 0), max(dy, 0)) + min(max(dx, dy), 0) - radius


def segment_distance(x: float, y: float, a: tuple[int, int], b: tuple[int, int]) -> float:
    vx, vy = b[0] - a[0], b[1] - a[1]
    amount = max(0, min(1, ((x - a[0]) * vx + (y - a[1]) * vy) / (vx * vx + vy * vy)))
    return math.hypot(x - a[0] - amount * vx, y - a[1] - amount * vy)


def icon_coverage(kind: str, x: float, y: float) -> bool:
    if kind == "white":
        return True
    if kind == "dot":
        return math.hypot(x - 12, y - 12) <= 5
    if kind == "check":
        return min(segment_distance(x, y, (5, 12), (10, 17)), segment_distance(x, y, (10, 17), (19, 7))) <= 1.5
    if kind == "cross":
        return min(segment_distance(x, y, (6, 6), (18, 18)), segment_distance(x, y, (6, 18), (18, 6))) <= 1.3
    if kind == "up":
        y = 24 - y
    elif kind == "left":
        x, y = y, 24 - x
    elif kind == "right":
        x, y = y, x
    return 9 <= y <= 16 and abs(x - 12) <= 16 - y


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def generate(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    pixels = bytearray(ATLAS_SIZE * ATLAS_SIZE * 4)
    regions = []
    entries = [(name, fill, border, None) for name, fill, border in PANELS]
    entries.extend((name, None, None, kind) for name, kind in ICONS)
    for index, (name, fill, border, kind) in enumerate(entries):
        origin_x = index % 8 * TILE_SIZE + GUTTER
        origin_y = index // 8 * TILE_SIZE + GUTTER
        for y in range(REGION_SIZE):
            for x in range(REGION_SIZE):
                accum = [0, 0, 0, 0]
                for sy in range(4):
                    for sx in range(4):
                        px, py = x + (sx + 0.5) / 4, y + (sy + 0.5) / 4
                        if kind:
                            color = (235, 245, 255) if kind != "white" else (255, 255, 255)
                            visible = icon_coverage(kind, px, py)
                        else:
                            distance = rounded_distance(px, py)
                            color = border if distance >= -1.5 else fill
                            visible = distance <= 0 and color is not None
                        if visible:
                            for channel in range(3):
                                accum[channel] += color[channel]
                            accum[3] += 255
                offset = ((origin_y + y) * ATLAS_SIZE + origin_x + x) * 4
                if accum[3]:
                    # Keep straight RGB at antialiased edges; alpha alone records coverage.
                    samples = accum[3] / 255
                    pixels[offset:offset + 4] = bytes([round(accum[c] / samples) for c in range(3)] + [round(accum[3] / 16)])
        region = {"name": name, "rect": [origin_x, origin_y, REGION_SIZE, REGION_SIZE]}
        if kind is None:
            region.update(draw_mode="nine_slice", slice=[6, 6, 6, 6], padding=[8.0, 8.0, 8.0, 8.0], minimum_size=[12.0, 12.0])
        regions.append(region)

    stride = ATLAS_SIZE * 4
    rows = b"".join(b"\x00" + pixels[y * stride:(y + 1) * stride] for y in range(ATLAS_SIZE))
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", ATLAS_SIZE, ATLAS_SIZE, 8, 6, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(rows, 9)) + png_chunk(b"IEND", b"")
    (directory / "source.png").write_bytes(png)
    # Semantic aliases share atlas artwork.
    parts = {region["name"]: region for region in regions}
    for name, source in (("list.background", "edit.normal"), ("list.row.normal", "button.normal"),
        ("list.row.disabled", "button.disabled"), ("scroll.track", "panel.normal"), ("scroll.thumb", "button.normal"),
        ("combo.normal", "button.normal"), ("combo.hover", "button.hover"), ("combo.open", "button.pressed"),
        ("combo.focused", "edit.focused"), ("combo.disabled", "button.disabled")):
        alias = {"name": name, "rect": parts[source]["rect"], "draw_mode": "nine_slice", "slice": [6, 6, 6, 6]}
        if name == "list.background":
            alias.update(padding=[8.0, 8.0, 8.0, 8.0], minimum_size=[12.0, 12.0])
        if name.startswith("combo."):
            alias.update(padding=[8.0, 6.0, 8.0, 6.0], minimum_size=[12.0, 12.0])
        regions.append(alias)
    lines = [
        "ui_skin asset;", "",
        'asset.texture = "engine/ui/skins/default/texture";',
        f"asset.atlas_extent = [{ATLAS_SIZE}, {ATLAS_SIZE}];",
        "asset.reference_density = 1.0;", "asset.toolkit_contract = \"widgets\";", "asset.regions = [",
    ]
    lines.extend("    " + json.dumps(region, separators=(", ", ": ")) + "," for region in regions)
    lines.extend(["];", "asset.colors = ["])
    lines.extend("    " + json.dumps(color, separators=(", ", ": ")) + "," for color in COLOR_ROLES)
    lines.extend(["];", 'asset.typography = {"default_font_size": 16.0};', ""])
    (directory / "atlas.nwb").write_bytes("\r\n".join(lines).encode("utf-8"))


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=DEFAULT_DIRECTORY)
    parser.add_argument("--tex-conv", type=Path, help="Also regenerate texture.nwb/.tex using the built tex_conv executable.")
    args = parser.parse_args(argv)
    directory = args.directory.resolve()
    generate(directory)
    if args.tex_conv:
        subprocess.run([str(args.tex_conv.resolve()), str(directory / "source.png"), "--output", str(directory / "texture"), "--force"], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
