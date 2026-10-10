"""Compare tooltip/menu snapshots, marker pixels, clipping, and atlas interiors."""
from __future__ import annotations

import math
import re

from window_capture_smoke import SmokeFailure
from probe_reference import encoded_marker, linear_channels, linear_rgb_bytes

NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiPopupToolsSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiPopupToolsSmoke: state sequence=(\d+) values=((?:\d+,){14}\d+)")
GEOMETRY = re.compile(rf"UiPopupToolsSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiPopupToolsSmoke: skin=(default|alternate)")
FIELDS = ("tooltip", "open", "cursor", "command", "commits", "anchor_clicks", "underlying", "sentinel_focused",
    "enabled", "reversed", "source_revision", "source_generation", "label_reads", "text_bytes", "list_selected")
RECTANGLES = ("anchor", "menu", "tooltip", "list", "viewport", "first", "second", "disabled", "last", "cursor_row",
    "sentinel", "counter")
MARKER_TOLERANCE = 5


def snapshot_from_logs(text):
    states, displays, skins = list(STATE.finditer(text)), list(DISPLAY.finditer(text)), list(SKIN.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence = int(state[1])
    values = tuple(map(int, state[2].split(",")))
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in GEOMETRY.finditer(text)
        if int(match[1]) == sequence}
    if any(name not in rectangles for name in RECTANGLES):
        return None
    display = tuple(map(float, displays[-1].groups()))
    geometry_values = (value for rectangle in rectangles.values() for value in rectangle)
    if not all(math.isfinite(value) for value in (*display, *geometry_values)) or not all(value > 0.0 for value in display):
        raise SmokeFailure("popup tools fixture reported invalid geometry or display metrics")
    return {"sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(FIELDS, values)),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles}


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def tooltip_ink_coverage(observed, background, foreground):
    actual, backdrop = linear_channels(observed), linear_channels(background)
    direction = tuple(target - base for target, base in zip(foreground, backdrop))
    coverage = sum((value - base) * component for value, base, component in zip(actual, backdrop, direction))
    coverage /= sum(component * component for component in direction)
    coverage = max(0.0, min(1.0, coverage))
    expected = linear_rgb_bytes(tuple(base + coverage * component for base, component in zip(backdrop, direction)))
    return coverage, max(abs(value - target) for value, target in zip(observed, expected))


def observe_popup_tools(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    probes = []

    def probe(name, position, color, tolerance=MARKER_TOLERANCE):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"popup tools probe '{name}' falls outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": [x, y], "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance})

    for index, value in enumerate((*snapshot["values"], snapshot["sequence"])):
        for part in range(2):
            marker = index * 2 + part
            probe(f"model_{index}_{part}", (18.0 + marker * 16.0, height / scale_y - 14.0), encoded_marker(value >> (part * 12)))
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    probe("anchor_panel_gutter", (28.0, 200.0), panel, 8)
    geometry_matches = True
    logical_width, logical_height = snapshot["logical_extent"]
    if snapshot["tooltip"]:
        tx, ty, tw, th = snapshot["rectangles"]["tooltip"]
        geometry_matches = geometry_matches and 0.0 <= tx <= logical_width - tw and 0.0 <= ty <= logical_height - th
        geometry_matches = geometry_matches and 0.0 < tw <= 320.0 and th > 12.0
        if tw > 20.0 and th > 12.0:
            probe("tooltip_normal_skin", (tx + tw - 12.0, ty + th - 6.0), (39, 48, 61), 8)
            foreground = (1.0, 0.58, 0.26) if skin == "alternate" else (1.0, 1.0, 1.0)
            left, top = round((tx + 8.0) * scale_x), round((ty + 8.0) * scale_y)
            right, bottom = round((tx + tw - 8.0) * scale_x), round((ty + th - 8.0) * scale_y)
            candidates = [(*tooltip_ink_coverage(rows[y][x], (39, 48, 61), foreground), rows[y][x], x, y)
                for y in range(max(0, top), min(height, bottom)) for x in range(max(0, left), min(width, right))]
            passing = [candidate for candidate in candidates if candidate[0] >= 0.45 and candidate[1] <= 12]
            best = max(passing, default=(0.0, 255, (0, 0, 0), left, top), key=lambda candidate: candidate[0])
            probes.append({"name": "tooltip_text_palette", "position": [best[3], best[4]],
                "expected": list(linear_rgb_bytes(foreground)), "observed": list(best[2]),
                "coverage": best[0], "error": best[1], "passed": bool(passing)})
    if snapshot["open"]:
        mx, my, mw, mh = snapshot["rectangles"]["menu"]
        geometry_matches = geometry_matches and 0.0 <= mx <= logical_width - mw and 0.0 <= my <= logical_height - mh
        geometry_matches = geometry_matches and abs(mw - 220.0) <= 0.75 and abs(mh - 180.0) <= 0.75
        if mw > 12.0 and mh > 12.0:
            probe("context_menu_popup_skin", (mx + 4.0, my + mh / 2.0), (26, 34, 45), 8)
        lx, ly, lw, lh = snapshot["rectangles"]["list"]
        if lw > 12.0 and lh > 12.0:
            probe("context_list_background", (lx + 4.0, ly + lh / 2.0), (15, 20, 28), 8)
        dx, dy, dw, dh = snapshot["rectangles"]["disabled"]
        if dw > 40.0 and dh > 8.0:
            probe("disabled_command_skin", (dx + dw - 20.0, dy + dh / 2.0),
                (54, 60, 42) if skin == "alternate" else (37, 43, 52), 8)
        rx, ry, rw, rh = snapshot["rectangles"]["cursor_row"]
        if rw > 40.0 and rh > 8.0:
            color = (38, 80, 123) if snapshot["cursor"] == snapshot["list_selected"] else (
                (49, 100, 63) if skin == "alternate" else (45, 60, 80))
            probe("context_cursor_row", (rx + rw - 20.0, ry + rh / 2.0), color, 8)
    sx, sy, sw, sh = snapshot["rectangles"]["sentinel"]
    # Menus may overlap this field; only inspect its independent focused/unfocused background when no overlay covers it.
    if not snapshot["open"] and sw > 32.0 and sh > 12.0:
        probe("outside_edit_focus_skin", (sx + sw - 16.0, sy + sh - 6.0),
            (18, 26, 37) if snapshot["sentinel_focused"] else (15, 20, 28), 8)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    bounded = snapshot["label_reads"] <= 5
    return {"extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "bounded": bounded, "geometry_matches": geometry_matches,
        "passed": model_matches and bounded and geometry_matches and (extent is None or (width, height) == extent)
            and (extra is None or extra(snapshot)) and all(item["passed"] for item in probes)}
