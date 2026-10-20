"""Decode frozen radio snapshots and compare model, atlas and placement pixels."""
from __future__ import annotations

import math
import re

from window_capture_smoke import SmokeFailure
from probe_common import center, snapshot_from_patterns
from probe_reference import compose, encoded_marker, linear_channels, sampled_tile

LIT_LABEL_READS = "label_reads"
LIT_DISABLED_SELECTED = "disabled_selected"
LIT_CHOICE_COUNT = "choice_count"
LIT_POPUP = "popup_"
LIT_ROW = "row"
LIT_INDICATOR = "indicator"
LIT_MARK = "mark"
LIT_VALUES = "values"
LIT_SEQUENCE = "sequence"
LIT_RECTANGLES = "rectangles"
LIT_NAME = "name"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_GROUP = "group"
LIT_DISABLED_GROUP = "disabled_group"
LIT_REMOVED = "removed"
LIT_DISABLED = "disabled_"
LIT_PARENT = "parent"
LIT_POPUP_GROUP = "popup_group"


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiRadioGroupSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiRadioGroupSmoke: state sequence=(\d+) values=((?:\d+,){21}\d+)")
GEOMETRY = re.compile(rf"UiRadioGroupSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiRadioGroupSmoke: skin=(default|alternate)")
FIELDS = ("main_selected", "main_cursor", "focus_code", "changes", "activations", "before_clicks", "after_clicks", "enabled", "source_revision", "source_generation", "reversed", "removed", "parent", "popup_selected",
    "popup_cursor", "popup_changes", "popup_activations", "popup_count", "focus_scope", LIT_LABEL_READS, LIT_DISABLED_SELECTED, LIT_CHOICE_COUNT)
KEYS = (10, 20, 30, 40, 50)
RECTANGLES = ("before", "group", "after", "open", "disabled_group", "parent", "popup_group", "popup_close", "disabled_row30", "disabled_indicator30", "disabled_mark30") + tuple(
        f"{prefix}{part}{key}" for prefix in ("", LIT_POPUP) for key in KEYS for part in (LIT_ROW, LIT_INDICATOR, LIT_MARK))
MARKER_TOLERANCE = 5
SKIN_TOLERANCE = 8
NORMAL = ((20, 27, 36), (81, 103, 129))
CHECKED = ((40, 102, 164), (110, 186, 245))
MARK = (235, 245, 255)


def snapshot_from_logs(text):
    snapshot = snapshot_from_patterns(text, STATE, DISPLAY, SKIN, GEOMETRY, FIELDS, RECTANGLES, fixture="radio")
    if snapshot is None:
        return None
    values, sequence, rectangles = snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE], snapshot[LIT_RECTANGLES]
    if any(rectangle[2] < 0.0 or rectangle[3] < 0.0 for rectangle in rectangles.values()):
        raise SmokeFailure("radio fixture reported a negative placement extent")
    if any(value > 0xFFFFFF for value in (*values, sequence)):
        raise SmokeFailure("radio fixture exceeds its two-part 24-bit displayed marker schema")
    return snapshot


def observe_radio_group(frame, snapshot, expected, *, extent=None, skin="default", extra=None, pressed=None):
    width, height, rows = frame
    scale = snapshot["scale"]
    rectangles = snapshot[LIT_RECTANGLES]
    probes = []

    def record(name, pixel, color, tolerance=SKIN_TOLERANCE):
        x, y = pixel
        if not 0 <= x < width or not 0 <= y < height:
            raise SmokeFailure(f"radio probe '{name}' falls outside {width}x{height}: ({x},{y})")
        observed = rows[y][x]
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        result = {LIT_NAME: name, "position": [x, y], LIT_EXPECTED: list(color), "observed": list(observed), "error": error, LIT_PASSED: error <= tolerance}
        probes.append(result)
        return result

    def probe(name, point, color, tolerance=SKIN_TOLERANCE):
        pixel = round(point[0] * scale[0]), round(point[1] * scale[1])
        return record(name, pixel, color, tolerance)

    for index, value in enumerate((*snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE])):
        for part in range(2):
            probe(f"model_{index}_{part}", (16.0 + 12.0 * (index * 2 + part), snapshot[LIT_LOGICAL_EXTENT][1] - 14.0), encoded_marker(value >> (part * 12)), MARKER_TOLERANCE)
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    gx, gy, gw, gh = rectangles[LIT_GROUP]
    probe("replacement_panel_gutter", (gx + gw - 8.0, gy + gh + 2.0), panel)
    geometry_matches = snapshot[LIT_LABEL_READS] <= 15 and snapshot[LIT_CHOICE_COUNT] in (4, 5)
    natural_height = 8.0 + snapshot[LIT_CHOICE_COUNT] * 24.0 + (snapshot[LIT_CHOICE_COUNT] - 1) * 4.0
    geometry_matches = geometry_matches and abs(gh - natural_height) <= 0.01
    geometry_matches = geometry_matches and abs(rectangles[LIT_DISABLED_GROUP][3] - natural_height) <= 0.01

    def contains(outer, inner):
        ox, oy, ow, oh = outer
        ix, iy, iw, ih = inner
        return iw > 0.0 and ih > 0.0 and ix >= ox - 0.01 and iy >= oy - 0.01 \
            and ix + iw <= ox + ow + 0.01 and iy + ih <= oy + oh + 0.01

    def choice(prefix, key, group, selected, enabled, backdrop):
        nonlocal geometry_matches
        indicator, mark = rectangles[f"{prefix}indicator{key}"], rectangles[f"{prefix}mark{key}"]
        row = rectangles[f"{prefix}row{key}"]
        if row[2] <= 0.0 or row[3] <= 0.0:
            geometry_matches = geometry_matches and key == snapshot[LIT_REMOVED] \
                and indicator[2:] == (0.0, 0.0) and mark[2:] == (0.0, 0.0)
            return
        geometry_matches = geometry_matches and key != snapshot[LIT_REMOVED] \
            and contains(group, row) and contains(row, indicator) and contains(indicator, mark)
        # At 14px these ASCII labels fit the authored 24px indicator, which determines natural row height.
        geometry_matches = geometry_matches and abs(row[3] - 24.0) <= 0.01 and abs(indicator[2] - 24.0) <= 0.01
        geometry_matches = geometry_matches and abs(indicator[3] - 24.0) <= 0.01
        geometry_matches = geometry_matches and abs(mark[2] - indicator[2] * 0.4) <= 0.01
        geometry_matches = geometry_matches and abs(mark[3] - indicator[3] * 0.4) <= 0.01
        if prefix == LIT_DISABLED and snapshot[LIT_PARENT] and contains(rectangles[LIT_PARENT], indicator) \
                and contains(rectangles[LIT_PARENT], mark):
            probes.append({LIT_NAME: f"{prefix}indicator{key}_occluded_by_parent_popup", "occluded": True, "occluding_rectangle": list(rectangles[LIT_PARENT]), "indicator_rectangle": list(indicator), "mark_rectangle": list(mark), LIT_PASSED: True})
            return
        fill, border = CHECKED if key == selected else NORMAL
        tint = (0.55, 0.55, 0.55, 0.6) if not enabled or key == 30 else ((0.85, 0.85, 0.85, 1.0) if pressed == (prefix, key) else (1.0, 1.0, 1.0, 1.0))
        ix, iy, iw, ih = indicator
        probe(f"{prefix}indicator{key}_fill", (ix + 4.0, iy + ih / 2.0), compose(linear_channels(fill), 1.0, tint, backdrop))
        # At scaled DPI the first border pixel can include alpha/filtering. Compare authored samples at each pixel center.
        candidates = []
        # The whole-row focus overlay crosses the indicator's left edge; its right border remains independent.
        left = max(0, math.floor((ix + iw - 1.5) * scale[0]))
        right = min(width, math.ceil((ix + iw) * scale[0]))
        top = round((iy + ih / 2.0) * scale[1])
        for px in range(left, right) if 0 <= top < height else ():
            source, alpha = sampled_tile("panel", indicator, (px, top), scale, fill, border)
            if alpha < 0.7:
                continue
            color = compose(source, alpha, tint, backdrop)
            observed = rows[top][px]
            error = max(abs(actual - reference) for actual, reference in zip(observed, color))
            candidates.append((error, px, color))
        if candidates:
            _, px, color = min(candidates)
            record(f"{prefix}indicator{key}_border", (px, top), color)
        else:
            probes.append({LIT_NAME: f"{prefix}indicator{key}_border", LIT_PASSED: False})
        mx, my, mw, mh = mark
        for name, u, v in (("center", 0.5, 0.5), ("offcenter", 0.60, 0.35)):
            pixel = round((mx + mw * u) * scale[0]), round((my + mh * v) * scale[1])
            source, alpha = sampled_tile("dot", mark, pixel, scale, fill, border)
            color = compose(source, alpha, tint, compose(linear_channels(fill), 1.0, tint, backdrop)) \
                if key == selected else compose(linear_channels(fill), 1.0, tint, backdrop)
            record(f"{prefix}mark{key}_{name}", pixel, color)

    for key in KEYS:
        choice("", key, rectangles[LIT_GROUP], snapshot["main_selected"], bool(snapshot["enabled"]), panel)
    choice(LIT_DISABLED, 30, rectangles[LIT_DISABLED_GROUP], snapshot[LIT_DISABLED_SELECTED], False, panel)
    if snapshot[LIT_PARENT]:
        popup = rectangles[LIT_PARENT]
        geometry_matches = geometry_matches and contains((0.0, 0.0, *snapshot[LIT_LOGICAL_EXTENT]), popup)
        geometry_matches = geometry_matches and abs(rectangles[LIT_POPUP_GROUP][3] - natural_height) <= 0.01
        for key in KEYS:
            choice(LIT_POPUP, key, rectangles[LIT_POPUP_GROUP], snapshot["popup_selected"], True, (26, 34, 45))
    else:
        for name in (LIT_PARENT, LIT_POPUP_GROUP, "popup_close"):
            geometry_matches = geometry_matches and rectangles[name][2:] == (0.0, 0.0)
        for key in KEYS:
            for part in (LIT_ROW, LIT_INDICATOR, LIT_MARK):
                geometry_matches = geometry_matches and rectangles[f"popup_{part}{key}"][2:] == (0.0, 0.0)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return {"extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "geometry_matches": geometry_matches, LIT_PASSED: model_matches and geometry_matches
            and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot)) and all(item[LIT_PASSED] for item in probes)}
