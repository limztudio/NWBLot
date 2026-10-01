"""Read frozen edit-box metadata and check matching presentation pixels."""
from __future__ import annotations

import math
import re

from interaction_smoke import linear_rgb_bytes
from window_capture_smoke import SmokeFailure

NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiEditSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiEditSmoke: state sequence=(\d+) primary=(\d+),(\d+),(\d+),(\d+),([01])"
    r" secondary=(\d+),(\d+),(\d+),(\d+),([01]) readonly=([01]) visible=([01])")
FIELD = re.compile(r"field=(primary|secondary)")
RECTANGLE = re.compile(rf"(bounds|content|caret|selection)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SCROLL = re.compile(rf"scroll=({NUMBER})")
TOLERANCE = 5
CARET_RGB = linear_rgb_bytes((0.95, 0.97, 1.0))


def text_hash(value):
    result = 2166136261
    for byte in value.encode("utf-8"):
        result = ((result ^ byte) * 16777619) & 0xFFFFFFFF
    return result


def model(value, anchor=None, caret=None, focused=False):
    length = len(value.encode("utf-8"))
    return (length, text_hash(value), length if anchor is None else anchor,
        length if caret is None else caret, int(focused))


def snapshot_from_logs(text):
    states = list(STATE.finditer(text))
    displays = list(DISPLAY.finditer(text))
    if not states or not displays:
        return None
    state = states[-1]
    sequence = int(state[1])
    fields = {"primary": {}, "secondary": {}}
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
            geometry["scroll"] = float(scroll[1])
    if any(len(geometry) != 5 for geometry in fields.values()):
        return None
    display = tuple(map(float, displays[-1].groups()))
    geometry_values = [value for geometry in fields.values() for name, rectangle in geometry.items()
        for value in ((rectangle,) if name == "scroll" else rectangle)]
    if not all(math.isfinite(value) for value in (*display, *geometry_values)) or not all(value > 0.0 for value in display):
        raise SmokeFailure("edit fixture reported invalid geometry or display metrics")
    return {"sequence": sequence, "primary": tuple(map(int, state.groups()[1:6])),
        "secondary": tuple(map(int, state.groups()[6:11])), "readonly": bool(int(state[12])),
        "visible": bool(int(state[13])), "logical_extent": display[:2], "scale": display[2:], "fields": fields}


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def encoded(value):
    return linear_rgb_bytes(tuple(((value >> shift) & 15) / 15.0 for shift in (0, 4, 8)))


def caret_required(field, state, readonly, visible, selection):
    return bool(state[4] and not selection and (field != "primary" or (visible and not readonly)))


def caret_probe_position(caret, content, scale_x, scale_y, width, height):
    x, y, caret_width, caret_height = caret
    column = round(x * scale_x)
    row = round((min(y + caret_height, content[1] + content[3]) - 0.75) * scale_y)
    if not 0 <= column < width or not 0 <= row < height:
        raise SmokeFailure(f"edit caret probe falls outside {width}x{height}: ({column},{row})")
    return column, row, max(0, row - 1), min(height, row + 2)


def observe_edit(frame, snapshot, primary, secondary, readonly, visible, *, extent=None, selection=False, scrolled=False):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    probes = []

    def probe(name, position, expected, *, tolerance=TOLERANCE):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"edit probe '{name}' falls outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, expected))
        probes.append({"name": name, "position": [x, y], "expected": list(expected),
            "observed": list(observed), "error": error, "passed": error <= tolerance})

    def caret_probe(field, caret, content):
        column, row, first_row, end_row = caret_probe_position(caret, content, scale_x, scale_y, width, height)
        expected = CARET_RGB
        pixels = [rows[py][column] for py in range(first_row, end_row)]
        error = min(max(abs(actual - reference) for actual, reference in zip(pixel, expected)) for pixel in pixels)
        probes.append({"name": f"{field}_caret", "position": [column, row], "expected": list(expected),
            "observed": [list(pixel) for pixel in pixels], "error": error, "passed": error <= TOLERANCE})

    values = (*snapshot["primary"], *snapshot["secondary"], int(snapshot["readonly"]),
        int(snapshot["visible"]), snapshot["sequence"])
    for index, value in enumerate(values):
        probe(f"model_{index}", (19.0 + index * 20.0, height / scale_y - 14.0), encoded(value))

    geometry_matches = True
    for field, state in (("primary", primary), ("secondary", secondary)):
        if field == "primary" and not visible:
            continue
        geometry = snapshot["fields"][field]
        bx, by, bw, bh = geometry["bounds"]
        cx, cy, cw, ch = geometry["content"]
        kx, ky, kw, kh = geometry["caret"]
        geometry_matches = geometry_matches and abs(bw - 260.0) <= 0.75 and abs(bh - 40.0) <= 0.75
        geometry_matches = geometry_matches and cw > 0.0 and ch > 0.0 and kw > 0.0 and kh > 0.0
        geometry_matches = geometry_matches and cx <= kx <= cx + cw + 0.75 and cy <= ky < cy + ch
        background = (18, 26, 37) if state[4] else (15, 20, 28)
        probe(f"{field}_skin", (bx + bw - 16.0, by + bh - 6.0), background, tolerance=8)
        # A clear gutter outside the control catches glyphs escaping its clip when the line scrolls.
        probe(f"{field}_clip_gutter", (bx + bw + 4.0, by + bh / 2.0), (27, 34, 44), tolerance=8)
        if caret_required(field, state, readonly, visible, selection):
            geometry_matches = geometry_matches and abs(kw * scale_x - 1.0) <= 0.01
            caret_probe(field, geometry["caret"], geometry["content"])
        if scrolled and state[4]:
            geometry_matches = geometry_matches and geometry["scroll"] > 0.0
        if selection and state[4]:
            sx, sy, sw, sh = geometry["selection"]
            geometry_matches = geometry_matches and sw > 1.0 and sh > 1.0
            if sw > 1.0 and sh > 1.0:
                # Selection is painted behind glyphs; the lower line edge avoids the glyph's ink.
                linear_background = tuple(channel / 255.0 / 12.92 if channel <= 10 else
                    ((channel / 255.0 + 0.055) / 1.055) ** 2.4 for channel in background)
                color = tuple(foreground * 0.75 + backdrop * 0.25
                    for foreground, backdrop in zip((0.20, 0.40, 0.78), linear_background))
                probe(f"{field}_selection", (sx + min(sw / 2.0, 4.0), min(sy + sh, cy + ch) - 2.0), linear_rgb_bytes(color))

    model_matches = snapshot["primary"] == primary and snapshot["secondary"] == secondary
    model_matches = model_matches and snapshot["readonly"] == readonly and snapshot["visible"] == visible
    return {"extent": [width, height], "snapshot": snapshot, "primary": list(primary), "secondary": list(secondary),
        "readonly": readonly, "visible": visible, "probes": probes,
        "passed": model_matches and geometry_matches and (extent is None or (width, height) == extent)
            and all(item["passed"] for item in probes)}
