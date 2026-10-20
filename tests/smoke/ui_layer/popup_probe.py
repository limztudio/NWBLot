"""Read frozen popup metadata and compare it with accepted presentation pixels."""
from __future__ import annotations

import re

from probe_common import snapshot_from_patterns
from window_capture_smoke import SmokeFailure
from probe_reference import encoded_marker, linear_rgb_bytes

LIT_POPUP = "popup"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_RIGHT = "right"
LIT_OPEN = "open"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_RECTANGLES = "rectangles"

NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiPopupSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiPopupSmoke: state sequence=(\d+) values=((?:\d+,){11}\d+)")
GEOMETRY = re.compile(rf"UiPopupSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiPopupSmoke: skin=(default|alternate)")
FIELDS = ("open", "modal", "underlying", "selected", "checked", "edit_focused", "outside_focused", "edge", "right", "visible", "text_bytes", "side")
RECTANGLES = ("popup", "viewport", "trigger", "modal", "counter", "outside", "first", "second", "check", "edit")
TOLERANCE = 5
BACKDROP_COLOR = (0.72, 0.02, 0.04)
SENTINEL_COLOR = (0.12, 0.52, 0.86)


def snapshot_from_logs(text):
    return snapshot_from_patterns(text, STATE, DISPLAY, SKIN, GEOMETRY, FIELDS, RECTANGLES, fixture=LIT_POPUP)




def expected_placement(snapshot):
    width, height = snapshot[LIT_LOGICAL_EXTENT]
    if snapshot["edge"]:
        return ((width - 284.0, height - 236.0, 220.0, 236.0), 3) if snapshot[LIT_RIGHT] else ((width - 220.0, height - 288.0, 220.0, 236.0), 1)
    return ((324.0, 64.0, 220.0, 236.0), 2) if snapshot[LIT_RIGHT] else ((200.0, 104.0, 220.0, 236.0), 0)


def observe_popup(frame, snapshot, expected, *, extent=None, skin="default"):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    dimmed = bool(snapshot[LIT_OPEN] and snapshot["modal"])
    probes = []

    def probe(name, position, color, tolerance=TOLERANCE):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"popup probe '{name}' falls outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": [x, y], LIT_EXPECTED: list(color), "observed": list(observed), "error": error, LIT_PASSED: error <= tolerance})

    for index, value in enumerate((*snapshot["values"], snapshot["sequence"])):
        probe(f"model_{index}", (19.0 + index * 20.0, height / scale_y - 14.0), encoded_marker(value, 0.6 if dimmed else 1.0))
    logical_width = snapshot[LIT_LOGICAL_EXTENT][0]
    probe("late_root_modal_sentinel", (logical_width - 32.0, 32.0), linear_rgb_bytes(tuple(value * (0.6 if dimmed else 1.0) for value in SENTINEL_COLOR)))
    bx, by, bw, bh = snapshot[LIT_RECTANGLES][LIT_POPUP]
    geometry_matches = True
    if snapshot[LIT_OPEN]:
        wanted, side = expected_placement(snapshot)
        geometry_matches = max(abs(actual - target) for actual, target in zip((bx, by, bw, bh), wanted)) <= 0.75
        geometry_matches = geometry_matches and snapshot["side"] == side
        viewport = (0.0, 0.0, *snapshot[LIT_LOGICAL_EXTENT])
        geometry_matches = geometry_matches and snapshot[LIT_RECTANGLES]["viewport"] == viewport
        # A clear popup gutter sits over an opaque red rectangle painted by a later host root.
        # The two atlas families have the same popup color and different UV tile positions.
        probe("popup_skin_over_later_root", (bx + bw - 16.0, by + bh - 16.0), (26, 34, 45), 8)
    else:
        if bw <= 0.0:
            bx, by, bw, bh = (200.0, 104.0, 220.0, 236.0)
        probe("closed_overlay_exposes_later_root", (bx + bw - 16.0, by + bh - 16.0), linear_rgb_bytes(BACKDROP_COLOR))
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return {"extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, LIT_PASSED: model_matches and geometry_matches and (extent is None or (width, height) == extent) and all(item[LIT_PASSED] for item in probes)}
