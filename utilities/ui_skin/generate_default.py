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
ATLAS_COLUMNS = ATLAS_SIZE // TILE_SIZE
RGB_CHANNEL_COUNT = 3
RGBA_CHANNEL_COUNT = 4
CHANNEL_MAX = 255
SAMPLES_PER_AXIS = 4
SAMPLE_COUNT = SAMPLES_PER_AXIS * SAMPLES_PER_AXIS
CORNER_RADIUS = 5.0
BORDER_WIDTH = 1.5
ICON_TINT = (235, 245, 255)
ICON_WHITE_TINT = (255, 255, 255)
ICON_CENTER = 12
DOT_RADIUS = 5
CHECK_START = (5, 12)
CHECK_BOTTOM = (10, 17)
CHECK_END = (19, 7)
CHECK_HALF_WIDTH = 1.5
CROSS_NEAR = 6
CROSS_FAR = 18
CROSS_HALF_WIDTH = 1.3
ARROW_BASE_Y = 9
ARROW_TIP_Y = 16
NINE_SLICE_BORDER = 6
CONTROL_PADDING = 8.0
COMBO_PADDING_Y = 6.0
MINIMUM_REGION_SIZE = 12.0
PNG_BIT_DEPTH = 8
PNG_COLOR_TYPE_RGBA = 6
PNG_COMPRESSION_LEVEL = 9
PNG_UINT32_FORMAT = ">I"
ATLAS_LINE_BREAK = "\r\n"
SOURCE_IMAGE_NAME = "source.png"
TEXTURE_OUTPUT_NAME = "texture"
TEX_CONV_OUTPUT_FLAG = "--output"
TEX_CONV_FORCE_FLAG = "--force"

FIELD_NAME = "name"
FIELD_RGBA = "rgba"
FIELD_RECT = "rect"
FIELD_DRAW_MODE = "draw_mode"
FIELD_SLICE = "slice"
FIELD_PADDING = "padding"
FIELD_MINIMUM_SIZE = "minimum_size"
DRAW_MODE_NINE_SLICE = "nine_slice"

KIND_WHITE = "white"
KIND_DOT = "dot"
KIND_CHECK = "check"
KIND_CROSS = "cross"
KIND_UP = "up"
KIND_DOWN = "down"
KIND_LEFT = "left"
KIND_RIGHT = "right"

REGION_PANEL_NORMAL = "panel.normal"
REGION_BUTTON_NORMAL = "button.normal"
REGION_BUTTON_HOVER = "button.hover"
REGION_BUTTON_PRESSED = "button.pressed"
REGION_BUTTON_DISABLED = "button.disabled"
REGION_EDIT_NORMAL = "edit.normal"
REGION_EDIT_FOCUSED = "edit.focused"
REGION_LIST_BACKGROUND = "list.background"

# Name, fill RGB, border RGB; RGB is authored in sRGB and alpha is straight coverage.
PANELS = [(REGION_PANEL_NORMAL, (24, 29, 37), (63, 77, 93)), ("window.normal", (27, 34, 44), (71, 86, 105)), ("window.title", (34, 47, 64), (71, 86, 105)), (REGION_BUTTON_NORMAL, (45, 60, 80), (79, 101, 126)), (REGION_BUTTON_HOVER, (60, 84, 112), (107, 147, 188)),
    (REGION_BUTTON_PRESSED, (33, 47, 65), (88, 128, 171)), (REGION_BUTTON_DISABLED, (37, 43, 52), (58, 65, 75)), (REGION_EDIT_NORMAL, (15, 20, 28), (65, 80, 100)), (REGION_EDIT_FOCUSED, (18, 26, 37), (97, 174, 240)), ("edit.disabled", (29, 33, 40), (54, 61, 70)),
    ("checkbox.normal", (20, 27, 36), (81, 103, 129)), ("checkbox.hover", (34, 48, 65), (109, 163, 211)), ("checkbox.checked", (40, 102, 164), (110, 186, 245)), ("checkbox.disabled", (32, 37, 45), (57, 65, 76)), ("radio.normal", (20, 27, 36), (81, 103, 129)),
    ("radio.checked", (40, 102, 164), (110, 186, 245)), ("list.normal", (17, 23, 31), (62, 77, 95)), ("list.row.hover", (39, 58, 80), (39, 58, 80)), ("list.row.selected", (38, 80, 123), (59, 113, 168)), ("popup.normal", (26, 34, 45), (81, 101, 126)),
    ("tooltip.normal", (39, 48, 61), (95, 112, 133)), ("scrollbar.track", (17, 23, 31), (40, 50, 63)), ("scrollbar.thumb.normal", (65, 82, 103), (82, 104, 130)), ("scrollbar.thumb.hover", (88, 119, 151), (116, 153, 190)),
    ("slider.track", (20, 29, 41), (63, 80, 101)), ("slider.thumb.normal", (69, 131, 185), (130, 193, 238)), ("slider.thumb.hover", (86, 164, 221), (166, 221, 255)), ("progress.track", (20, 29, 41), (63, 80, 101)),
    ("progress.fill", (53, 131, 192), (97, 176, 233)), ("focus.overlay", None, (124, 194, 251)), ("separator", (65, 80, 99), (65, 80, 99)),]
ICONS = [("checkbox.mark", KIND_CHECK), ("radio.mark", KIND_DOT), ("combo.arrow", KIND_DOWN), ("window.close", KIND_CROSS), ("window.collapse", KIND_DOWN), ("scrollbar.arrow.up", KIND_UP), ("scrollbar.arrow.down", KIND_DOWN), ("scrollbar.arrow.left", KIND_LEFT),
    ("scrollbar.arrow.right", KIND_RIGHT), ("white", KIND_WHITE),]

# Linear RGB and straight alpha, in UiSkinColorRole order.
COLOR_ROLES = [{FIELD_NAME: "text.normal", FIELD_RGBA: [0.92, 0.94, 0.98, 1.0]}, {FIELD_NAME: "text.disabled", FIELD_RGBA: [0.48, 0.50, 0.55, 1.0]}, {FIELD_NAME: "text.tooltip", FIELD_RGBA: [1.0, 1.0, 1.0, 1.0]}, {FIELD_NAME: "edit.background", FIELD_RGBA: [0.08, 0.10, 0.14, 1.0]},
    {FIELD_NAME: "edit.selection", FIELD_RGBA: [0.20, 0.40, 0.78, 0.75]},
    {FIELD_NAME: "edit.inactive_selection", FIELD_RGBA: [0.28, 0.31, 0.38, 0.55]},
    {FIELD_NAME: "edit.caret", FIELD_RGBA: [0.95, 0.97, 1.0, 1.0]},
    {FIELD_NAME: "edit.preedit", FIELD_RGBA: [0.50, 0.72, 1.0, 1.0]},
    {FIELD_NAME: "scrollbar.track", FIELD_RGBA: [0.08, 0.10, 0.14, 1.0]},
    {FIELD_NAME: "scrollbar.thumb", FIELD_RGBA: [0.35, 0.40, 0.48, 1.0]},
    {FIELD_NAME: "scrollbar.disabled", FIELD_RGBA: [0.25, 0.28, 0.32, 1.0]},
    {FIELD_NAME: "popup.backdrop", FIELD_RGBA: [0.0, 0.0, 0.0, 0.4]},
    {FIELD_NAME: "control.hover_tint", FIELD_RGBA: [1.08, 1.08, 1.08, 1.0]},
    {FIELD_NAME: "control.pressed_tint", FIELD_RGBA: [0.85, 0.85, 0.85, 1.0]},
    {FIELD_NAME: "control.disabled_tint", FIELD_RGBA: [0.55, 0.55, 0.55, 0.6]},
    {FIELD_NAME: "progress.track_tint", FIELD_RGBA: [1.0, 1.0, 1.0, 1.0]},
    {FIELD_NAME: "progress.fill_tint", FIELD_RGBA: [1.0, 1.0, 1.0, 1.0]},]


def rounded_distance(x: float, y: float, radius: float = CORNER_RADIUS) -> float:
    half = REGION_SIZE / 2
    dx = abs(x - half) - (half - radius)
    dy = abs(y - half) - (half - radius)
    return math.hypot(max(dx, 0), max(dy, 0)) + min(max(dx, dy), 0) - radius


def segment_distance(x: float, y: float, a: tuple[int, int], b: tuple[int, int]) -> float:
    vx, vy = b[0] - a[0], b[1] - a[1]
    amount = max(0, min(1, ((x - a[0]) * vx + (y - a[1]) * vy) / (vx * vx + vy * vy)))
    return math.hypot(x - a[0] - amount * vx, y - a[1] - amount * vy)


def icon_coverage(kind: str, x: float, y: float) -> bool:
    if kind == KIND_WHITE:
        return True
    if kind == KIND_DOT:
        return math.hypot(x - ICON_CENTER, y - ICON_CENTER) <= DOT_RADIUS
    if kind == KIND_CHECK:
        return min(segment_distance(x, y, CHECK_START, CHECK_BOTTOM), segment_distance(x, y, CHECK_BOTTOM, CHECK_END)) <= CHECK_HALF_WIDTH
    if kind == KIND_CROSS:
        return min(segment_distance(x, y, (CROSS_NEAR, CROSS_NEAR), (CROSS_FAR, CROSS_FAR)), segment_distance(x, y, (CROSS_NEAR, CROSS_FAR), (CROSS_FAR, CROSS_NEAR))) <= CROSS_HALF_WIDTH
    if kind == KIND_UP:
        y = REGION_SIZE - y
    elif kind == KIND_LEFT:
        x, y = y, REGION_SIZE - x
    elif kind == KIND_RIGHT:
        x, y = y, x
    return ARROW_BASE_Y <= y <= ARROW_TIP_Y and abs(x - ICON_CENTER) <= ARROW_TIP_Y - y


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(PNG_UINT32_FORMAT, len(payload)) + kind + payload + struct.pack(PNG_UINT32_FORMAT, zlib.crc32(kind + payload))


def generate(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    pixels = bytearray(ATLAS_SIZE * ATLAS_SIZE * RGBA_CHANNEL_COUNT)
    regions = []
    entries = [(name, fill, border, None) for name, fill, border in PANELS]
    entries.extend((name, None, None, kind) for name, kind in ICONS)
    for index, (name, fill, border, kind) in enumerate(entries):
        origin_x = index % ATLAS_COLUMNS * TILE_SIZE + GUTTER
        origin_y = index // ATLAS_COLUMNS * TILE_SIZE + GUTTER
        for y in range(REGION_SIZE):
            for x in range(REGION_SIZE):
                accum = [0, 0, 0, 0]
                for sy in range(SAMPLES_PER_AXIS):
                    for sx in range(SAMPLES_PER_AXIS):
                        px, py = x + (sx + 0.5) / SAMPLES_PER_AXIS, y + (sy + 0.5) / SAMPLES_PER_AXIS
                        if kind:
                            color = ICON_TINT if kind != KIND_WHITE else ICON_WHITE_TINT
                            visible = icon_coverage(kind, px, py)
                        else:
                            distance = rounded_distance(px, py)
                            color = border if distance >= -BORDER_WIDTH else fill
                            visible = distance <= 0 and color is not None
                        if visible:
                            for channel in range(RGB_CHANNEL_COUNT):
                                accum[channel] += color[channel]
                            accum[3] += CHANNEL_MAX
                offset = ((origin_y + y) * ATLAS_SIZE + origin_x + x) * RGBA_CHANNEL_COUNT
                if accum[3]:
                    # Keep straight RGB at antialiased edges; alpha alone records coverage.
                    samples = accum[3] / CHANNEL_MAX
                    pixels[offset:offset + RGBA_CHANNEL_COUNT] = bytes([round(accum[c] / samples) for c in range(RGB_CHANNEL_COUNT)] + [round(accum[3] / SAMPLE_COUNT)])
        region = {FIELD_NAME: name, FIELD_RECT: [origin_x, origin_y, REGION_SIZE, REGION_SIZE]}
        if kind is None:
            region.update({FIELD_DRAW_MODE: DRAW_MODE_NINE_SLICE, FIELD_SLICE: [NINE_SLICE_BORDER, NINE_SLICE_BORDER, NINE_SLICE_BORDER, NINE_SLICE_BORDER], FIELD_PADDING: [CONTROL_PADDING, CONTROL_PADDING, CONTROL_PADDING, CONTROL_PADDING], FIELD_MINIMUM_SIZE: [MINIMUM_REGION_SIZE, MINIMUM_REGION_SIZE]})
        regions.append(region)

    stride = ATLAS_SIZE * RGBA_CHANNEL_COUNT
    rows = b"".join(b"\x00" + pixels[y * stride:(y + 1) * stride] for y in range(ATLAS_SIZE))
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", ATLAS_SIZE, ATLAS_SIZE, PNG_BIT_DEPTH, PNG_COLOR_TYPE_RGBA, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(rows, PNG_COMPRESSION_LEVEL)) + png_chunk(b"IEND", b"")
    (directory / SOURCE_IMAGE_NAME).write_bytes(png)
    # Semantic aliases share atlas artwork.
    parts = {region[FIELD_NAME]: region for region in regions}
    for name, source in ((REGION_LIST_BACKGROUND, REGION_EDIT_NORMAL), ("list.row.normal", REGION_BUTTON_NORMAL), ("list.row.disabled", REGION_BUTTON_DISABLED), ("scroll.track", REGION_PANEL_NORMAL), ("scroll.thumb", REGION_BUTTON_NORMAL),
        ("combo.normal", REGION_BUTTON_NORMAL), ("combo.hover", REGION_BUTTON_HOVER), ("combo.open", REGION_BUTTON_PRESSED), ("combo.focused", REGION_EDIT_FOCUSED), ("combo.disabled", REGION_BUTTON_DISABLED)):
        alias = {FIELD_NAME: name, FIELD_RECT: parts[source][FIELD_RECT], FIELD_DRAW_MODE: DRAW_MODE_NINE_SLICE, FIELD_SLICE: [NINE_SLICE_BORDER, NINE_SLICE_BORDER, NINE_SLICE_BORDER, NINE_SLICE_BORDER]}
        if name == REGION_LIST_BACKGROUND:
            alias.update({FIELD_PADDING: [CONTROL_PADDING, CONTROL_PADDING, CONTROL_PADDING, CONTROL_PADDING], FIELD_MINIMUM_SIZE: [MINIMUM_REGION_SIZE, MINIMUM_REGION_SIZE]})
        if name.startswith("combo."):
            alias.update({FIELD_PADDING: [CONTROL_PADDING, COMBO_PADDING_Y, CONTROL_PADDING, COMBO_PADDING_Y], FIELD_MINIMUM_SIZE: [MINIMUM_REGION_SIZE, MINIMUM_REGION_SIZE]})
        regions.append(alias)
    lines = ["ui_skin asset;", "", 'asset.texture = "engine/ui/skins/default/texture";', f"asset.atlas_extent = [{ATLAS_SIZE}, {ATLAS_SIZE}];", "asset.reference_density = 1.0;", "asset.toolkit_contract = \"widgets\";", "asset.regions = [",]
    lines.extend("    " + json.dumps(region, separators=(", ", ": ")) + "," for region in regions)
    lines.extend(["];", "asset.colors = ["])
    lines.extend("    " + json.dumps(color, separators=(", ", ": ")) + "," for color in COLOR_ROLES)
    lines.extend(["];", 'asset.typography = {"default_font_size": 16.0};', ""])
    (directory / "atlas.nwb").write_bytes(ATLAS_LINE_BREAK.join(lines).encode("utf-8"))


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=DEFAULT_DIRECTORY)
    parser.add_argument("--tex-conv", type=Path, help="Also regenerate texture.nwb/.tex using the built tex_conv executable.")
    args = parser.parse_args(argv)
    directory = args.directory.resolve()
    generate(directory)
    if args.tex_conv:
        subprocess.run([str(args.tex_conv.resolve()), str(directory / SOURCE_IMAGE_NAME), TEX_CONV_OUTPUT_FLAG, str(directory / TEXTURE_OUTPUT_NAME), TEX_CONV_FORCE_FLAG], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
