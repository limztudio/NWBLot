"""Compare frozen virtual-list metadata with numeric GPU marker pixels."""
from __future__ import annotations

import math
import re

from window_capture_smoke import SmokeFailure
from probe_reference import encoded_marker

NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiListSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiListSmoke: state sequence=(\d+) values=((?:\d+,){11}\d+)")
GEOMETRY = re.compile(rf"UiListSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiListSmoke: skin=(default|alternate)")
FIELDS = ("selected", "cursor", "count", "first", "past", "label_reads", "focused", "reversed", "removed",
    "visible", "commits", "underlying")
RECTANGLES = ("list", "viewport", "track", "thumb", "selected_row", "counter")
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
        raise SmokeFailure("list fixture reported invalid geometry or display metrics")
    return {"sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(FIELDS, values)),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles}


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def observe_list(frame, snapshot, expected, *, extent=None, skin="default", row_height=24.0, extra=None):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    probes = []

    def probe(name, position, color, tolerance=MARKER_TOLERANCE):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"list probe '{name}' falls outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": [x, y], "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance})

    # Every scalar is represented by two 12-bit RGB markers, retaining all 100000 row keys and indices.
    for index, value in enumerate((*snapshot["values"], snapshot["sequence"])):
        for part in range(2):
            marker = index * 2 + part
            probe(f"model_{index}_{part}", (19.0 + marker * 20.0, height / scale_y - 14.0),
                encoded_marker(value >> (part * 12)))
    first, past = snapshot["first"], snapshot["past"]
    viewport = snapshot["rectangles"]["viewport"]
    bounded = first <= past <= snapshot["count"] and snapshot["label_reads"] <= max(0, past - first) + 2
    if snapshot["visible"]:
        bounded = bounded and past - first <= math.ceil(viewport[3] / row_height) + 2
        # A dark textured frame and its clear outer gutter verify clipping independently of model markers.
        bx, by, bw, bh = snapshot["rectangles"]["list"]
        if bw > 12.0 and bh > 12.0:
            probe("list_skin_gutter", (bx + 4.0, by + bh / 2.0), (17, 23, 31), 8)
            probe("list_outer_panel_gutter", (bx + bw + 4.0, by + bh / 2.0),
                (39, 63, 34) if skin == "alternate" else (24, 29, 37), 8)
        tx, ty, tw, th = snapshot["rectangles"]["track"]
        sx, sy, sw, sh = snapshot["rectangles"]["thumb"]
        if tw > 0.0 and th > sh + 8.0:
            free_top, free_bottom = sy - ty, ty + th - sy - sh
            track_y = ty + free_top / 2.0 if free_top > free_bottom else sy + sh + free_bottom / 2.0
            probe("scroll_track_skin", (tx + tw / 2.0, track_y),
                (39, 63, 34) if skin == "alternate" else (24, 29, 37), 8)
            probe("scroll_thumb_skin", (sx + sw / 2.0, sy + sh / 2.0),
                (49, 100, 63) if skin == "alternate" else (45, 60, 80), 8)
        rx, ry, rw, rh = snapshot["rectangles"]["selected_row"]
        if rw > 40.0 and rh > 8.0 and viewport[1] + 4.0 <= ry + rh / 2.0 <= viewport[1] + viewport[3] - 4.0:
            probe("selected_row_skin", (rx + rw - 20.0, ry + rh / 2.0), (38, 80, 123), 8)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return {"extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "bounded": bounded, "passed": model_matches and bounded and (extent is None or (width, height) == extent)
            and (extra is None or extra(snapshot)) and all(item["passed"] for item in probes)}
