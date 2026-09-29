"""Check searchable combo source/query snapshots against the composed GPU layer."""
from __future__ import annotations

import math
import re

from interaction_smoke import linear_rgb_bytes
from window_capture_smoke import SmokeFailure

NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiSearchComboSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiSearchComboSmoke: state sequence=(\d+) values=((?:\d+,){21}\d+)")
GEOMETRY = re.compile(rf"UiSearchComboSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiSearchComboSmoke: skin=(default|alternate)")
FIELDS = ("selected", "cursor", "full_count", "count", "first", "past", "label_reads", "open", "query_bytes",
    "query_hash", "query_anchor", "query_caret", "query_focused", "focused", "commits", "filter_revision", "reversed",
    "removed", "enabled", "source_revision", "underlying", "composing")
RECTANGLES = ("trigger", "popup", "list", "viewport", "track", "thumb", "cursor_row", "counter", "query",
    "query_content", "query_caret", "query_selection")
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
        raise SmokeFailure("combo fixture reported invalid geometry or display metrics")
    return {"sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(FIELDS, values)),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles}


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def encoded(value):
    return linear_rgb_bytes(tuple(((value >> shift) & 15) / 15.0 for shift in (0, 4, 8)))


def observe_search_combo(frame, snapshot, expected, *, extent=None, skin="default", row_height=24.0, extra=None):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    probes = []

    def probe(name, position, color, tolerance=MARKER_TOLERANCE):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"combo probe '{name}' falls outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": [x, y], "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance})

    for index, value in enumerate((*snapshot["values"], snapshot["sequence"])):
        for part in range(2):
            marker = index * 2 + part
            probe(f"model_{index}_{part}", (16.0 + marker * 12.0, height / scale_y - 14.0),
                encoded(value >> (part * 12)))
    first, past = snapshot["first"], snapshot["past"]
    viewport = snapshot["rectangles"]["viewport"]
    bounded = snapshot["label_reads"] <= 2
    geometry_matches = True
    if snapshot["open"]:
        bounded = first <= past <= snapshot["count"] and snapshot["label_reads"] <= max(0, past - first) + 3
        bounded = bounded and past - first <= math.ceil(viewport[3] / row_height) + 2
        bx, by, bw, bh = snapshot["rectangles"]["popup"]
        tx, ty, tw, th = snapshot["rectangles"]["trigger"]
        logical_width, logical_height = snapshot["logical_extent"]
        geometry_matches = 0.0 <= bx <= logical_width - bw and 0.0 <= by <= logical_height - bh
        geometry_matches = geometry_matches and by >= ty + th - 0.75
        if bw > 12.0 and bh > 12.0:
            probe("popup_skin_gutter", (bx + 4.0, by + bh / 2.0), (26, 34, 45), 8)
        lx, ly, lw, lh = snapshot["rectangles"]["list"]
        if lw > 12.0 and lh > 12.0:
            probe("list_skin_gutter", (lx + 4.0, ly + lh / 2.0), (17, 23, 31), 8)
        sx, sy, sw, sh = snapshot["rectangles"]["thumb"]
        track_x, track_y, track_w, track_h = snapshot["rectangles"]["track"]
        if track_w > 0.0 and track_h > sh + 8.0:
            free_top, free_bottom = sy - track_y, track_y + track_h - sy - sh
            clear_y = track_y + free_top / 2.0 if free_top > free_bottom else sy + sh + free_bottom / 2.0
            probe("scroll_track_skin", (track_x + track_w / 2.0, clear_y),
                (39, 63, 34) if skin == "alternate" else (24, 29, 37), 8)
            probe("scroll_thumb_skin", (sx + sw / 2.0, sy + sh / 2.0),
                (49, 100, 63) if skin == "alternate" else (45, 60, 80), 8)
        rx, ry, rw, rh = snapshot["rectangles"]["cursor_row"]
        if rw > 40.0 and rh > 8.0 and viewport[1] + 4.0 <= ry + rh / 2.0 <= viewport[1] + viewport[3] - 4.0:
            row_color = (38, 80, 123) if snapshot["cursor"] == snapshot["selected"] else (
                (49, 100, 63) if skin == "alternate" else (45, 60, 80))
            probe("preview_row_committed_skin", (rx + rw - 20.0, ry + rh / 2.0), row_color, 8)
    tx, ty, tw, th = snapshot["rectangles"]["trigger"]
    if tw > 100.0 and th > 16.0:
        if not snapshot["enabled"]:
            field_color = (54, 60, 42) if skin == "alternate" else (37, 43, 52)
        elif snapshot["open"]:
            field_color = (36, 76, 48) if skin == "alternate" else (33, 47, 65)
        elif snapshot["focused"]:
            field_color = (18, 26, 37)
        else:
            field_color = (49, 100, 63) if skin == "alternate" else (45, 60, 80)
        probe("combo_trigger_skin", (tx + tw - 56.0, ty + th / 2.0), field_color, 8)
    if snapshot["open"]:
        qx, qy, qw, qh = snapshot["rectangles"]["query"]
        content = snapshot["rectangles"]["query_content"]
        geometry_matches = geometry_matches and qw > 0.0 and qh == 36.0 and content[2] > 0.0
        background = (18, 26, 37) if snapshot["query_focused"] else (15, 20, 28)
        if qw > 32.0 and qh > 12.0:
            probe("search_query_edit_skin", (qx + qw - 16.0, qy + qh - 6.0), background, 8)
        if snapshot["query_focused"] and snapshot["query_anchor"] != snapshot["query_caret"]:
            sx, sy, sw, sh = snapshot["rectangles"]["query_selection"]
            geometry_matches = geometry_matches and sw > 1.0 and sh > 1.0
            if sw > 1.0 and sh > 1.0:
                linear_background = tuple(channel / 255.0 / 12.92 if channel <= 10 else
                    ((channel / 255.0 + 0.055) / 1.055) ** 2.4 for channel in background)
                color = tuple(foreground * 0.75 + backdrop * 0.25
                    for foreground, backdrop in zip((0.20, 0.40, 0.78), linear_background))
                probe("search_query_selection", (sx + min(sw / 2.0, 4.0), min(sy + sh, content[1] + content[3]) - 2.0),
                    linear_rgb_bytes(color))
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return {"extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "bounded": bounded, "geometry_matches": geometry_matches,
        "passed": model_matches and bounded and geometry_matches and (extent is None or (width, height) == extent)
            and (extra is None or extra(snapshot)) and all(item["passed"] for item in probes)}


def text_hash(value):
    result = 2166136261
    for byte in value.encode("utf-8"):
        result = ((result ^ byte) * 16777619) & 0xFFFFFFFF
    return result & 0xFFFFFF
