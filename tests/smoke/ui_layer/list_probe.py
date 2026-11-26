from __future__ import annotations

import math

from probe_common import FrameProbeRecorder, compile_smoke_patterns, snapshot_from_patterns

LIT_LIST = "list"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_RECTANGLES = "rectangles"
LIT_COUNT = "count"
LIT_ALTERNATE = "alternate"

FIELDS = ("selected", "cursor", "count", "first", "past", "label_reads", "focused", "reversed", "removed", "visible", "commits", "underlying")
RECTANGLES = ("list", "viewport", "first_row", "track", "thumb", "selected_row", "counter")
DISPLAY, STATE, GEOMETRY, SKIN = compile_smoke_patterns("UiListSmoke", len(FIELDS))
MARKER_TOLERANCE = 5


def snapshot_from_logs(text):
    return snapshot_from_patterns(text, STATE, DISPLAY, SKIN, GEOMETRY, FIELDS, RECTANGLES, fixture=LIT_LIST)


def observe_list(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, _ = frame
    _, scale_y = snapshot["scale"]
    recorder = FrameProbeRecorder(frame, snapshot["scale"], LIT_LIST)
    probes = recorder.probes
    probe = lambda name, position, color, tolerance=MARKER_TOLERANCE: recorder.probe(name, position, color, tolerance)

    recorder.probe_markers((*snapshot["values"], snapshot["sequence"]), 19.0, 20.0, height / scale_y - 14.0, tolerance=MARKER_TOLERANCE)
    first, past = snapshot["first"], snapshot["past"]
    viewport = snapshot[LIT_RECTANGLES]["viewport"]
    bounded = first <= past <= snapshot[LIT_COUNT] and snapshot["label_reads"] <= max(0, past - first) + 2
    if snapshot["visible"]:
        row_height = snapshot[LIT_RECTANGLES]["first_row"][3]
        bounded = bounded and (past == first == 0 if snapshot[LIT_COUNT] == 0 else row_height > 0.0 and past - first <= math.ceil(viewport[3] / row_height) + 2)
        bx, by, bw, bh = snapshot[LIT_RECTANGLES][LIT_LIST]
        if bw > 12.0 and bh > 12.0:
            probe("list_skin_gutter", (bx + 4.0, by + bh / 2.0), (17, 23, 31), 8)
            probe("list_outer_panel_gutter", (bx + bw + 4.0, by + bh / 2.0), (39, 63, 34) if skin == LIT_ALTERNATE else (24, 29, 37), 8)
        tx, ty, tw, th = snapshot[LIT_RECTANGLES]["track"]
        sx, sy, sw, sh = snapshot[LIT_RECTANGLES]["thumb"]
        if tw > 0.0 and th > sh + 8.0:
            free_top, free_bottom = sy - ty, ty + th - sy - sh
            track_y = ty + free_top / 2.0 if free_top > free_bottom else sy + sh + free_bottom / 2.0
            probe("scroll_track_skin", (tx + tw / 2.0, track_y), (39, 63, 34) if skin == LIT_ALTERNATE else (24, 29, 37), 8)
            probe("scroll_thumb_skin", (sx + sw / 2.0, sy + sh / 2.0), (49, 100, 63) if skin == LIT_ALTERNATE else (45, 60, 80), 8)
        rx, ry, rw, rh = snapshot[LIT_RECTANGLES]["selected_row"]
        if rw > 40.0 and rh > 8.0 and viewport[1] + 4.0 <= ry + rh / 2.0 <= viewport[1] + viewport[3] - 4.0:
            probe("selected_row_skin", (rx + rw - 20.0, ry + rh / 2.0), (38, 80, 123), 8)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return {"extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "bounded": bounded, LIT_PASSED: model_matches and bounded and (extent is None or (width, height) == extent)
            and (extra is None or extra(snapshot)) and all(item[LIT_PASSED] for item in probes)}
