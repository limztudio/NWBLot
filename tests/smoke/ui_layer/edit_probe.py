"""Read frozen edit-box metadata and check matching presentation pixels."""
from __future__ import annotations

import math
import re

from window_capture_smoke import SmokeFailure
from probe_reference import encoded_marker, linear_channels, linear_rgb_bytes

LIT_UTF_8 = "utf-8"
LIT_PRIMARY = "primary"
LIT_SECONDARY = "secondary"
LIT_SCROLL = "scroll"
LIT_SEQUENCE = "sequence"
LIT_READONLY = "readonly"
LIT_VISIBLE = "visible"
LIT_SCALE = "scale"
LIT_FIELDS = "fields"
LIT_NAME = "name"
LIT_POSITION = "position"
LIT_EXPECTED = "expected"
LIT_OBSERVED = "observed"
LIT_ERROR = "error"
LIT_PASSED = "passed"
LIT_CONTENT = "content"
LIT_CARET = "caret"

NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiEditSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiEditSmoke: state sequence=(\d+) primary=(\d+),(\d+),(\d+),(\d+),([01])" r" secondary=(\d+),(\d+),(\d+),(\d+),([01]) readonly=([01]) visible=([01])")
FIELD = re.compile(r"field=(primary|secondary)")
RECTANGLE = re.compile(rf"(bounds|content|caret|selection)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SCROLL = re.compile(rf"scroll=({NUMBER})")
TOLERANCE = 5
CARET_RGB = linear_rgb_bytes((0.95, 0.97, 1.0))


def text_hash(value):
    result = 2166136261
    for byte in value.encode(LIT_UTF_8):
        result = ((result ^ byte) * 16777619) & 0xFFFFFFFF
    return result


def model(value, anchor=None, caret=None, focused=False):
    length = len(value.encode(LIT_UTF_8))
    return (length, text_hash(value), length if anchor is None else anchor, length if caret is None else caret, int(focused))


def snapshot_from_logs(text):
    states = list(STATE.finditer(text))
    displays = list(DISPLAY.finditer(text))
    if not states or not displays:
        return None
    state = states[-1]
    sequence = int(state[1])
    fields = {LIT_PRIMARY: {}, LIT_SECONDARY: {}}
    for line in text.splitlines():
        if f"sequence={sequence} " not in line:
            continue
        field = FIELD.search(line)
        if not field:
            continue
        geometry = fields[field[1]]
        geometry.update({match[1]: tuple(map(float, match.groups()[1:])) for match in RECTANGLE.finditer(line)})
        scroll = SCROLL.search(line)
        if scroll:
            geometry[LIT_SCROLL] = float(scroll[1])
    if any(len(geometry) != 5 for geometry in fields.values()):
        return None
    display = tuple(map(float, displays[-1].groups()))
    geometry_values = [value for geometry in fields.values() for name, rectangle in geometry.items() for value in ((rectangle,) if name == LIT_SCROLL else rectangle)]
    if not all(math.isfinite(value) for value in (*display, *geometry_values)) or not all(value > 0.0 for value in display):
        raise SmokeFailure("edit fixture reported invalid geometry or display metrics")
    return {LIT_SEQUENCE: sequence, LIT_PRIMARY: tuple(map(int, state.groups()[1:6])), LIT_SECONDARY: tuple(map(int, state.groups()[6:11])), LIT_READONLY: bool(int(state[12])),
        LIT_VISIBLE: bool(int(state[13])), "logical_extent": display[:2], LIT_SCALE: display[2:], LIT_FIELDS: fields}


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def caret_required(field, state, readonly, visible, selection):
    return bool(state[4] and not selection and (field != LIT_PRIMARY or (visible and not readonly)))


def caret_pixel_column(logical_left, scale_x):
    """Select the one physical column covered by a one-pixel caret.

    Pixel centers are n + 0.5; the left edge is inclusive.
    """
    return math.ceil(logical_left * scale_x - 0.5)


def caret_probe_position(caret, content, scale_x, scale_y, width, height):
    x, y, caret_width, caret_height = caret
    column = caret_pixel_column(x, scale_x)
    row = round((min(y + caret_height, content[1] + content[3]) - 0.75) * scale_y)
    if not 0 <= column < width or not 0 <= row < height:
        raise SmokeFailure(f"edit caret probe falls outside {width}x{height}: ({column},{row})")
    return column, row, max(0, row - 1), min(height, row + 2)


def observe_edit(frame, snapshot, primary, secondary, readonly, visible, *, extent=None, selection=False, scrolled=False):
    width, height, rows = frame
    scale_x, scale_y = snapshot[LIT_SCALE]
    probes = []

    def probe(name, position, expected, *, tolerance=TOLERANCE):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"edit probe '{name}' falls outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, expected))
        probes.append({LIT_NAME: name, LIT_POSITION: [x, y], LIT_EXPECTED: list(expected), LIT_OBSERVED: list(observed), LIT_ERROR: error, LIT_PASSED: error <= tolerance})

    def caret_probe(field, caret, content):
        column, row, first_row, end_row = caret_probe_position(caret, content, scale_x, scale_y, width, height)
        expected = CARET_RGB
        pixels = [rows[py][column] for py in range(first_row, end_row)]
        error = min(max(abs(actual - reference) for actual, reference in zip(pixel, expected)) for pixel in pixels)
        probes.append({LIT_NAME: f"{field}_caret", LIT_POSITION: [column, row], LIT_EXPECTED: list(expected), LIT_OBSERVED: [list(pixel) for pixel in pixels], LIT_ERROR: error, LIT_PASSED: error <= TOLERANCE})

    values = (*snapshot[LIT_PRIMARY], *snapshot[LIT_SECONDARY], int(snapshot[LIT_READONLY]), int(snapshot[LIT_VISIBLE]), snapshot[LIT_SEQUENCE])
    for index, value in enumerate(values):
        probe(f"model_{index}", (19.0 + index * 20.0, height / scale_y - 14.0), encoded_marker(value))

    geometry_matches = True
    for field, state in ((LIT_PRIMARY, primary), (LIT_SECONDARY, secondary)):
        if field == LIT_PRIMARY and not visible:
            continue
        geometry = snapshot[LIT_FIELDS][field]
        bx, by, bw, bh = geometry["bounds"]
        cx, cy, cw, ch = geometry[LIT_CONTENT]
        kx, ky, kw, kh = geometry[LIT_CARET]
        geometry_matches = geometry_matches and abs(bw - 260.0) <= 0.75 and abs(bh - 40.0) <= 0.75
        geometry_matches = geometry_matches and cw > 0.0 and ch > 0.0 and kw > 0.0 and kh > 0.0
        geometry_matches = geometry_matches and cx <= kx <= cx + cw + 0.75 and cy <= ky < cy + ch
        background = (18, 26, 37) if state[4] else (15, 20, 28)
        probe(f"{field}_skin", (bx + bw - 16.0, by + bh - 6.0), background, tolerance=8)
        # A clear gutter outside the control catches glyphs escaping its clip when the line scrolls.
        probe(f"{field}_clip_gutter", (bx + bw + 4.0, by + bh / 2.0), (27, 34, 44), tolerance=8)
        if caret_required(field, state, readonly, visible, selection):
            geometry_matches = geometry_matches and abs(kw * scale_x - 1.0) <= 0.01
            caret_probe(field, geometry[LIT_CARET], geometry[LIT_CONTENT])
        if scrolled and state[4]:
            geometry_matches = geometry_matches and geometry[LIT_SCROLL] > 0.0
        if selection and state[4]:
            sx, sy, sw, sh = geometry["selection"]
            geometry_matches = geometry_matches and sw > 1.0 and sh > 1.0
            if sw > 1.0 and sh > 1.0:
                # Selection is painted behind glyphs; the lower line edge avoids the glyph's ink.
                linear_background = linear_channels(background)
                color = tuple(foreground * 0.75 + backdrop * 0.25 for foreground, backdrop in zip((0.20, 0.40, 0.78), linear_background))
                probe(f"{field}_selection", (sx + min(sw / 2.0, 4.0), min(sy + sh, cy + ch) - 2.0), linear_rgb_bytes(color))

    model_matches = snapshot[LIT_PRIMARY] == primary and snapshot[LIT_SECONDARY] == secondary
    model_matches = model_matches and snapshot[LIT_READONLY] == readonly and snapshot[LIT_VISIBLE] == visible
    return {"extent": [width, height], "snapshot": snapshot, LIT_PRIMARY: list(primary), LIT_SECONDARY: list(secondary), LIT_READONLY: readonly, LIT_VISIBLE: visible, "probes": probes,
        LIT_PASSED: model_matches and geometry_matches and (extent is None or (width, height) == extent) and all(item[LIT_PASSED] for item in probes)}
