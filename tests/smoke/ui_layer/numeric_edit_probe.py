"""Compare exact numeric values, drafts, accepted geometry, and all GPU markers."""
from __future__ import annotations

import math
import re
import struct

from window_capture_smoke import SmokeFailure
from probe_reference import encoded_marker, linear_channels, linear_rgb_bytes


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiNumericEditSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiNumericEditSmoke: state sequence=(\d+) values=((?:\d+,){29}\d+)")
GEOMETRY = re.compile(rf"UiNumericEditSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiNumericEditSmoke: skin=(default|alternate)")
FIELDS = ("integer_bits", "float_bits", "i_bytes", "i_hash", "i_anchor", "i_caret", "i_status", "i_dirty", "i_focus",
    "f_bytes", "f_hash", "f_anchor", "f_caret", "f_status", "f_dirty", "f_focus", "clipboard_bytes", "clipboard_hash",
    "clipboard_anchor", "clipboard_caret", "clipboard_focus", "enabled", "readonly", "clamp", "commits", "cancels",
    "rejects", "clamps", "restored", "coherent")
RECTANGLES = tuple(f"{field}_{part}" for field in ("integer", "float", "clipboard")
    for part in ("bounds", "content", "caret", "selection"))
MARKER_PARTS = tuple(6 if index < 2 else 3 if index in (3, 10, 17) else 2 for index in range(len(FIELDS))) + (2,)
MARKER_TOLERANCE = 5


def text_hash(text):
    value = 2166136261
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def integer_bits(value):
    return value & 0xFFFFFFFFFFFFFFFF


def float_bits(value):
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def draft_fields(prefix, text, *, anchor=None, caret=None, status=None, dirty=None):
    size = len(text.encode("utf-8"))
    fields = {f"{prefix}_bytes": size, f"{prefix}_hash": text_hash(text)}
    if anchor is not None:
        fields[f"{prefix}_anchor"] = anchor
    if caret is not None:
        fields[f"{prefix}_caret"] = caret
    if status is not None:
        fields[f"{prefix}_status"] = status
    if dirty is not None:
        fields[f"{prefix}_dirty"] = int(dirty)
    return fields


def snapshot_from_logs(text):
    states, displays, skins = list(STATE.finditer(text)), list(DISPLAY.finditer(text)), list(SKIN.finditer(text))
    if not states or not displays or not skins:
        return None
    state, display = states[-1], tuple(map(float, displays[-1].groups()))
    sequence, values = int(state[1]), tuple(map(int, state[2].split(",")))
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in GEOMETRY.finditer(text)
        if int(match[1]) == sequence}
    if any(name not in rectangles for name in RECTANGLES):
        return None
    geometry_values = (value for rectangle in rectangles.values() for value in rectangle)
    if not all(math.isfinite(value) for value in (*display, *geometry_values)) or not all(value > 0.0 for value in display):
        raise SmokeFailure("numeric fixture reported invalid geometry or display metrics")
    if any(value > 0xFFFFFFFFFFFFFFFF for value in values):
        raise SmokeFailure("numeric fixture state exceeds the unsigned 64-bit log schema")
    return {"sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(FIELDS, values)),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles}


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def ink_coverage(observed, background, foreground):
    # Small glyphs can be antialiased throughout. Require ink along the expected color ray,
    # with appreciable coverage, instead of requiring a fully opaque core pixel.
    actual, backdrop = linear_channels(observed), linear_channels(background)
    direction = tuple(target - base for target, base in zip(foreground, backdrop))
    denominator = sum(component * component for component in direction)
    coverage = sum((value - base) * component for value, base, component in zip(actual, backdrop, direction)) / denominator
    coverage = max(0.0, min(1.0, coverage))
    expected = linear_rgb_bytes(tuple(base + coverage * component for base, component in zip(backdrop, direction)))
    residual = max(abs(value - target) for value, target in zip(observed, expected))
    return coverage, residual, expected


def observe_numeric_edit(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    probes = []

    def probe(name, position, color, tolerance=MARKER_TOLERANCE):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"numeric probe '{name}' falls outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": [x, y], "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance})

    marker = 0
    for index, (value, parts) in enumerate(zip((*snapshot["values"], snapshot["sequence"]), MARKER_PARTS)):
        for part in range(parts):
            probe(f"model_{index}_{part}", (11.0 + marker * 8.0, snapshot["logical_extent"][1] - 13.0),
                encoded_marker(value >> (part * 12)))
            marker += 1
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    probe("panel_gutter", (28.0, 330.0), panel, 8)
    geometry_matches = marker == 73 and snapshot["coherent"] == 1
    for field, prefix in (("integer", "i"), ("float", "f"), ("clipboard", "clipboard")):
        bx, by, bw, bh = snapshot["rectangles"][f"{field}_bounds"]
        cx, cy, cw, ch = snapshot["rectangles"][f"{field}_content"]
        kx, ky, kw, kh = snapshot["rectangles"][f"{field}_caret"]
        sx, sy, sw, sh = snapshot["rectangles"][f"{field}_selection"]
        enabled = bool(snapshot["enabled"]) or field == "clipboard"
        focused = bool(snapshot[f"{prefix}_focus"]) if prefix != "clipboard" else bool(snapshot["clipboard_focus"])
        geometry_matches = geometry_matches and abs(bw - 400.0) <= 0.75 and abs(bh - 40.0) <= 0.75
        geometry_matches = geometry_matches and cw > 0.0 and ch > 0.0 and kw > 0.0 and kh > 0.0
        geometry_matches = geometry_matches and cx >= bx and cy >= by and cx + cw <= bx + bw + 0.75
        geometry_matches = geometry_matches and cy + ch <= by + bh + 0.75 and cx <= kx <= cx + cw + 0.75
        background = (29, 33, 40) if not enabled else (18, 26, 37) if focused else (15, 20, 28)
        probe(f"{field}_edit_skin", (bx + bw - 16.0, by + bh - 6.0), background, 5 if focused else 8)
        probe(f"{field}_clip_gutter", (bx + bw + 4.0, by + bh / 2.0), panel, 8)
        anchor, caret = snapshot[f"{prefix}_anchor"], snapshot[f"{prefix}_caret"]
        if field != "clipboard" and snapshot[f"{prefix}_bytes"] and anchor == caret:
            foreground = (0.92, 0.94, 0.98) if enabled else (0.48, 0.50, 0.55)
            caret_column = round(kx * scale_x)
            left, right = max(0, math.ceil(cx * scale_x)), min(width, math.floor((cx + min(cw, 200.0)) * scale_x))
            top, bottom = max(0, math.ceil(cy * scale_y)), min(height, math.floor((cy + ch) * scale_y))
            candidates = []
            for y in range(top, bottom):
                for x in range(left, right):
                    if abs(x - caret_column) <= 2:
                        continue
                    coverage, residual, color = ink_coverage(rows[y][x], background, foreground)
                    if coverage >= 0.25:
                        candidates.append((residual, -coverage, x, y, color, rows[y][x]))
            closest = min(candidates, default=None)
            if closest is None:
                probes.append({"name": f"{field}_text_ink", "coverage": 0.0, "passed": False})
            else:
                error, negative_coverage, x, y, color, observed = closest
                probes.append({"name": f"{field}_text_ink", "position": [x, y], "expected": list(color),
                    "observed": list(observed), "coverage": -negative_coverage, "error": error, "passed": error <= 5})
        if focused and anchor != caret:
            geometry_matches = geometry_matches and sw > 1.0 and sh > 1.0
            if sw > 1.0 and sh > 1.0:
                linear_background = linear_channels(background)
                color = tuple(foreground * 0.75 + backdrop * 0.25
                    for foreground, backdrop in zip((0.20, 0.40, 0.78), linear_background))
                probe(f"{field}_selection", (sx + min(sw / 2.0, 4.0), min(sy + sh, cy + ch) - 2.0),
                    linear_rgb_bytes(color))
        elif focused and enabled and (field == "clipboard" or not snapshot["readonly"]):
            geometry_matches = geometry_matches and abs(kw * scale_x - 1.0) <= 0.01
            column = round(kx * scale_x)
            row = round((min(ky + kh, cy + ch) - 0.75) * scale_y)
            if not 0 <= column < width or not 0 <= row < height:
                raise SmokeFailure(f"numeric caret falls outside {width}x{height}: ({column},{row})")
            color = linear_rgb_bytes((0.95, 0.97, 1.0))
            pixels = [rows[py][column] for py in range(max(0, row - 1), min(height, row + 2))]
            error = min(max(abs(actual - reference) for actual, reference in zip(pixel, color)) for pixel in pixels)
            probes.append({"name": f"{field}_caret", "position": [column, row], "expected": list(color),
                "observed": [list(pixel) for pixel in pixels], "error": error, "passed": error <= MARKER_TOLERANCE})
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    extra_matches = extra is None or extra(snapshot)
    return {"extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "geometry_matches": geometry_matches, "model_matches": model_matches, "extra_matches": extra_matches,
        "marker_count": marker, "passed": model_matches and geometry_matches and extra_matches
            and (extent is None or (width, height) == extent) and all(item["passed"] for item in probes)}
