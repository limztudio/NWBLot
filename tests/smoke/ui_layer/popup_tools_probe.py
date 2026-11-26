from __future__ import annotations

from probe_common import FrameProbeRecorder, compile_smoke_patterns, ink_coverage, snapshot_from_patterns
from probe_reference import linear_rgb_bytes

LIT_NAME = "name"
LIT_POSITION = "position"
LIT_EXPECTED = "expected"
LIT_OBSERVED = "observed"
LIT_ERROR = "error"
LIT_PASSED = "passed"
LIT_ALTERNATE = "alternate"
LIT_TOOLTIP = "tooltip"
LIT_RECTANGLES = "rectangles"
LIT_OPEN = "open"

FIELDS = ("tooltip", "open", "cursor", "command", "commits", "anchor_clicks", "underlying", "sentinel_focused", "enabled", "reversed", "source_revision", "source_generation", "label_reads", "text_bytes", "list_selected")
RECTANGLES = ("anchor", "menu", "tooltip", "list", "viewport", "first", "second", "disabled", "last", "cursor_row", "sentinel", "counter")
DISPLAY, STATE, GEOMETRY, SKIN = compile_smoke_patterns("UiPopupToolsSmoke", len(FIELDS))
MARKER_TOLERANCE = 5


def snapshot_from_logs(text):
    return snapshot_from_patterns(text, STATE, DISPLAY, SKIN, GEOMETRY, FIELDS, RECTANGLES, fixture="popup tools")


def tooltip_ink_coverage(observed, background, foreground):
    coverage, residual, _ = ink_coverage(observed, background, foreground)
    return coverage, residual


def observe_popup_tools(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    recorder = FrameProbeRecorder(frame, snapshot["scale"], "popup tools")
    probes = recorder.probes
    probe = lambda name, position, color, tolerance=MARKER_TOLERANCE: recorder.probe(name, position, color, tolerance)

    recorder.probe_markers((*snapshot["values"], snapshot["sequence"]), 18.0, 16.0, height / scale_y - 14.0, tolerance=MARKER_TOLERANCE)
    panel = (39, 63, 34) if skin == LIT_ALTERNATE else (24, 29, 37)
    probe("anchor_panel_gutter", (28.0, 200.0), panel, 8)
    geometry_matches = True
    logical_width, logical_height = snapshot["logical_extent"]
    if snapshot[LIT_TOOLTIP]:
        tx, ty, tw, th = snapshot[LIT_RECTANGLES][LIT_TOOLTIP]
        geometry_matches = geometry_matches and 0.0 <= tx <= logical_width - tw and 0.0 <= ty <= logical_height - th
        geometry_matches = geometry_matches and 0.0 < tw <= 320.0 and th > 12.0
        if tw > 20.0 and th > 12.0:
            probe("tooltip_normal_skin", (tx + tw - 12.0, ty + th - 6.0), (39, 48, 61), 8)
            foreground = (1.0, 0.58, 0.26) if skin == LIT_ALTERNATE else (1.0, 1.0, 1.0)
            left, top = round((tx + 8.0) * scale_x), round((ty + 8.0) * scale_y)
            right, bottom = round((tx + tw - 8.0) * scale_x), round((ty + th - 8.0) * scale_y)
            candidates = [(*tooltip_ink_coverage(rows[y][x], (39, 48, 61), foreground), rows[y][x], x, y) for y in range(max(0, top), min(height, bottom)) for x in range(max(0, left), min(width, right))]
            passing = [candidate for candidate in candidates if candidate[0] >= 0.45 and candidate[1] <= 12]
            best = max(passing, default=(0.0, 255, (0, 0, 0), left, top), key=lambda candidate: candidate[0])
            probes.append({LIT_NAME: "tooltip_text_palette", LIT_POSITION: [best[3], best[4]], LIT_EXPECTED: list(linear_rgb_bytes(foreground)), LIT_OBSERVED: list(best[2]), "coverage": best[0], LIT_ERROR: best[1], LIT_PASSED: bool(passing)})
    if snapshot[LIT_OPEN]:
        mx, my, mw, mh = snapshot[LIT_RECTANGLES]["menu"]
        geometry_matches = geometry_matches and 0.0 <= mx <= logical_width - mw and 0.0 <= my <= logical_height - mh
        geometry_matches = geometry_matches and abs(mw - 220.0) <= 0.75 and abs(mh - 180.0) <= 0.75
        if mw > 12.0 and mh > 12.0:
            probe("context_menu_popup_skin", (mx + 4.0, my + mh / 2.0), (26, 34, 45), 8)
        lx, ly, lw, lh = snapshot[LIT_RECTANGLES]["list"]
        if lw > 12.0 and lh > 12.0:
            probe("context_list_background", (lx + 4.0, ly + lh / 2.0), (15, 20, 28), 8)
        dx, dy, dw, dh = snapshot[LIT_RECTANGLES]["disabled"]
        if dw > 40.0 and dh > 8.0:
            probe("disabled_command_skin", (dx + dw - 20.0, dy + dh / 2.0), (54, 60, 42) if skin == LIT_ALTERNATE else (37, 43, 52), 8)
        rx, ry, rw, rh = snapshot[LIT_RECTANGLES]["cursor_row"]
        if rw > 40.0 and rh > 8.0:
            color = (38, 80, 123) if snapshot["cursor"] == snapshot["list_selected"] else ((49, 100, 63) if skin == LIT_ALTERNATE else (45, 60, 80))
            probe("context_cursor_row", (rx + rw - 20.0, ry + rh / 2.0), color, 8)
    sx, sy, sw, sh = snapshot[LIT_RECTANGLES]["sentinel"]
    if not snapshot[LIT_OPEN] and sw > 32.0 and sh > 12.0:
        probe("outside_edit_focus_skin", (sx + sw - 16.0, sy + sh - 6.0), (18, 26, 37) if snapshot["sentinel_focused"] else (15, 20, 28), 8)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    bounded = snapshot["label_reads"] <= 5
    return {"extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "bounded": bounded, "geometry_matches": geometry_matches,
        LIT_PASSED: model_matches and bounded and geometry_matches and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot)) and all(item[LIT_PASSED] for item in probes)}
