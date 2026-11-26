from __future__ import annotations

from probe_common import FrameProbeRecorder, compile_smoke_patterns, snapshot_from_patterns
from probe_reference import linear_rgb_bytes

LIT_POPUP = "popup"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_RIGHT = "right"
LIT_OPEN = "open"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_RECTANGLES = "rectangles"

FIELDS = ("open", "modal", "underlying", "selected", "checked", "edit_focused", "outside_focused", "edge", "right", "visible", "text_bytes", "side")
RECTANGLES = ("popup", "viewport", "trigger", "modal", "counter", "outside", "first", "second", "check", "edit")
DISPLAY, STATE, GEOMETRY, SKIN = compile_smoke_patterns("UiPopupSmoke", len(FIELDS))
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
    width, height, _ = frame
    _, scale_y = snapshot["scale"]
    dimmed = bool(snapshot[LIT_OPEN] and snapshot["modal"])
    recorder = FrameProbeRecorder(frame, snapshot["scale"], LIT_POPUP)
    probes = recorder.probes
    probe = lambda name, position, color, tolerance=TOLERANCE: recorder.probe(name, position, color, tolerance)

    recorder.probe_markers((*snapshot["values"], snapshot["sequence"]), 19.0, 20.0, height / scale_y - 14.0, parts=1, tolerance=TOLERANCE, dim=0.6 if dimmed else 1.0)
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
        probe("popup_skin_over_later_root", (bx + bw - 16.0, by + bh - 16.0), (26, 34, 45), 8)
    else:
        if bw <= 0.0:
            bx, by, bw, bh = (200.0, 104.0, 220.0, 236.0)
        probe("closed_overlay_exposes_later_root", (bx + bw - 16.0, by + bh - 16.0), linear_rgb_bytes(BACKDROP_COLOR))
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return {"extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, LIT_PASSED: model_matches and geometry_matches and (extent is None or (width, height) == extent) and all(item[LIT_PASSED] for item in probes)}
