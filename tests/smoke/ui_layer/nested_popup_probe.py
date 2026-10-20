"""Match nested-popup markers, accepted focus scope, geometry, and atlas interiors."""
from __future__ import annotations

import math
import re

from window_capture_smoke import SmokeFailure
from probe_reference import encoded_marker

LIT_SEARCH = "search"
LIT_MENU = "menu"
LIT_LABEL_READS = "label_reads"
LIT_COMBO = "combo"
LIT_COMBO_POPUP = "combo_popup"
LIT_SEARCH_POPUP = "search_popup"
LIT_SEARCH_QUERY = "search_query"
LIT_SEQUENCE = "sequence"
LIT_SKIN = "skin"
LIT_VALUES = "values"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_SCALE = "scale"
LIT_RECTANGLES = "rectangles"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_PARENT = "parent"
LIT_CHILD = "child"
LIT_FOCUS_CODE = "focus_code"

NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiNestedPopupSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiNestedPopupSmoke: state sequence=(\d+) values=((?:\d+,){28}\d+)")
GEOMETRY = re.compile(rf"UiNestedPopupSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiNestedPopupSmoke: skin=(default|alternate)")
FIELDS = ("parent", "child", "focus_scope", "focus_code", "before_clicks", "after_clicks", "child_clicks", "outside", "before_bytes", "after_bytes", "child_bytes", "before_selected", "after_selected", "combo", "combo_selected",
    LIT_SEARCH, "search_selected", "query_bytes", LIT_MENU, "menu_cursor", "command", LIT_LABEL_READS, "raw_child", "raw_combo", "raw_search", "raw_menu", "popup_targets", "popup_count", "child_parent_scope")
RECTANGLES = ("open", "counter", "parent", "child", "before_button", "before_edit", "before_list", "before_row2", "child_open", "after_button", "after_edit", "after_list", "after_row2", "child_action", "child_edit", "close_branch",
    LIT_COMBO, LIT_COMBO_POPUP, "combo_row2", LIT_SEARCH, LIT_SEARCH_POPUP, LIT_SEARCH_QUERY, "search_row2", "menu_anchor", LIT_MENU, "menu_row2")


def snapshot_from_logs(text):
    states, displays, skins = list(STATE.finditer(text)), list(DISPLAY.finditer(text)), list(SKIN.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence = int(state[1])
    values = tuple(map(int, state[2].split(",")))
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in GEOMETRY.finditer(text) if int(match[1]) == sequence}
    if any(name not in rectangles for name in RECTANGLES):
        return None
    display = tuple(map(float, displays[-1].groups()))
    if not all(math.isfinite(value) for value in (*display, *(value for rect in rectangles.values() for value in rect))):
        raise SmokeFailure("nested popup scene reported nonfinite geometry")
    if not all(value > 0.0 for value in display):
        raise SmokeFailure("nested popup scene reported invalid display metrics")
    return {LIT_SEQUENCE: sequence, LIT_SKIN: skins[-1][1], LIT_VALUES: values, **dict(zip(FIELDS, values)), LIT_LOGICAL_EXTENT: display[:2], LIT_SCALE: display[2:], LIT_RECTANGLES: rectangles}


def observe_nested_popup(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale_x, scale_y = snapshot[LIT_SCALE]
    probes = []

    def probe(name, position, color, tolerance=8):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"nested popup probe '{name}' falls outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": [x, y], LIT_EXPECTED: list(color), "observed": list(observed), "error": error, LIT_PASSED: error <= tolerance})

    for index, value in enumerate((*snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE])):
        for part in range(2):
            marker = index * 2 + part
            probe(f"model_{index}_{part}", (16.0 + marker * 12.0, height / scale_y - 14.0), encoded_marker(value >> (part * 12)), 5)
    probe("root_panel_skin", (28.0, 92.0), (39, 63, 34) if skin == "alternate" else (24, 29, 37))
    logical_width, logical_height = snapshot[LIT_LOGICAL_EXTENT]
    geometry_matches = True
    for flag, rectangle in ((LIT_PARENT, LIT_PARENT), (LIT_CHILD, LIT_CHILD), (LIT_COMBO, LIT_COMBO_POPUP), (LIT_SEARCH, LIT_SEARCH_POPUP), (LIT_MENU, LIT_MENU)):
        if snapshot[flag]:
            x, y, w, h = snapshot[LIT_RECTANGLES][rectangle]
            geometry_matches = geometry_matches and w > 0 and h > 0 and 0 <= x <= logical_width - w and 0 <= y <= logical_height - h
    if snapshot[LIT_PARENT] and not any(snapshot[name] for name in (LIT_COMBO, LIT_SEARCH, LIT_MENU)):
        px, py, pw, ph = snapshot[LIT_RECTANGLES][LIT_PARENT]
        geometry_matches = geometry_matches and abs(pw - 300.0) < 0.75 and abs(ph - 420.0) < 0.75
        probe("parent_popup_skin", (px + 4.0, py + ph - 14.0), (26, 34, 45))
        for name, focus_code in (("before_edit", 6), ("after_edit", 11)):
            x, y, w, h = snapshot[LIT_RECTANGLES][name]
            probe(name + "_skin", (x + w - 12.0, y + h - 6.0), (18, 26, 37) if snapshot[LIT_FOCUS_CODE] == focus_code else (15, 20, 28))
        for name in ("before_list", "after_list"):
            x, y, w, h = snapshot[LIT_RECTANGLES][name]
            probe(name + "_background", (x + 4.0, y + h / 2.0), (15, 20, 28))
    if snapshot[LIT_CHILD]:
        px, py, pw, ph = snapshot[LIT_RECTANGLES][LIT_PARENT]
        cx, cy, cw, ch = snapshot[LIT_RECTANGLES][LIT_CHILD]
        geometry_matches = geometry_matches and cx >= px + pw and abs(cw - 220.0) < 0.75 and abs(ch - 178.0) < 0.75
        probe("child_outside_parent_clip", (cx + cw - 14.0, cy + ch - 14.0), (26, 34, 45))
        x, y, w, h = snapshot[LIT_RECTANGLES]["child_edit"]
        probe("child_editor_skin", (x + w - 12.0, y + h - 6.0), (18, 26, 37) if snapshot[LIT_FOCUS_CODE] == 15 else (15, 20, 28))
    if snapshot[LIT_SEARCH]:
        x, y, w, h = snapshot[LIT_RECTANGLES][LIT_SEARCH_QUERY]
        probe("nested_search_editor", (x + w - 12.0, y + h - 6.0), (18, 26, 37))
    if snapshot[LIT_MENU]:
        x, y, w, h = snapshot[LIT_RECTANGLES][LIT_MENU]
        probe("nested_context_popup_skin", (x + 4.0, y + h - 14.0), (26, 34, 45))
    bounded = snapshot[LIT_LABEL_READS] <= 20
    model_matches = snapshot[LIT_SKIN] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return {"extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "bounded": bounded, "geometry_matches": geometry_matches,
        LIT_PASSED: model_matches and bounded and geometry_matches and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot)) and all(probe[LIT_PASSED] for probe in probes)}
