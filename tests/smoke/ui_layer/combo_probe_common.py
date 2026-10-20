"""Shared accepted-snapshot and displayed-pixel contract for combo smoke scenes."""
from __future__ import annotations

import math
import re

from window_capture_smoke import SmokeFailure
from probe_reference import encoded_marker, linear_channels, linear_rgb_bytes

LIT_SEQUENCE = "sequence"
LIT_SKIN = "skin"
LIT_VALUES = "values"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_SCALE = "scale"
LIT_RECTANGLES = "rectangles"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_LABEL_READS = "label_reads"
LIT_OPEN = "open"
LIT_COUNT = "count"
LIT_TRIGGER = "trigger"
LIT_ALTERNATE = "alternate"
LIT_QUERY_FOCUSED = "query_focused"

NUMBER = r"[0-9.eE+-]+"
MARKER_TOLERANCE = 5


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


class ComboProbe:
    def __init__(self, prefix, fields, rectangles, *, searchable=False):
        self.fields = fields
        self.rectangles = rectangles
        self.searchable = searchable
        self.marker_origin = 16.0 if searchable else 18.0
        self.marker_stride = 12.0 if searchable else 16.0
        self.display = re.compile(rf"{prefix}: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
        self.state = re.compile(rf"{prefix}: state sequence=(\d+) values=((?:\d+,){{{len(fields) - 1}}}\d+)")
        self.geometry = re.compile(rf"{prefix}: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
        self.skin = re.compile(rf"{prefix}: skin=(default|alternate)")

    def snapshot(self, text):
        states, displays, skins = list(self.state.finditer(text)), list(self.display.finditer(text)), list(self.skin.finditer(text))
        if not states or not displays or not skins:
            return None
        state = states[-1]
        sequence = int(state[1])
        values = tuple(map(int, state[2].split(",")))
        rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in self.geometry.finditer(text) if int(match[1]) == sequence}
        if any(name not in rectangles for name in self.rectangles):
            return None
        display = tuple(map(float, displays[-1].groups()))
        geometry_values = (value for rectangle in rectangles.values() for value in rectangle)
        if not all(math.isfinite(value) for value in (*display, *geometry_values)) or not all(value > 0.0 for value in display):
            raise SmokeFailure("combo fixture reported invalid geometry or display metrics")
        return {LIT_SEQUENCE: sequence, LIT_SKIN: skins[-1][1], LIT_VALUES: values, **dict(zip(self.fields, values)), LIT_LOGICAL_EXTENT: display[:2], LIT_SCALE: display[2:], LIT_RECTANGLES: rectangles}

    def observe(self, frame, snapshot, expected, *, extent=None, skin="default", extra=None):
        width, height, rows = frame
        scale_x, scale_y = snapshot[LIT_SCALE]
        probes = []

        def probe(name, position, color, tolerance=MARKER_TOLERANCE):
            x, y = round(position[0] * scale_x), round(position[1] * scale_y)
            if not 1 <= x < width - 1 or not 1 <= y < height - 1:
                raise SmokeFailure(f"combo probe '{name}' falls outside {width}x{height}: ({x},{y})")
            pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
            observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
            error = max(abs(actual - reference) for actual, reference in zip(observed, color))
            probes.append({"name": name, "position": [x, y], LIT_EXPECTED: list(color), "observed": list(observed), "error": error, LIT_PASSED: error <= tolerance})

        for index, value in enumerate((*snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE])):
            for part in range(2):
                marker = index * 2 + part
                probe(f"model_{index}_{part}", (self.marker_origin + marker * self.marker_stride, height / scale_y - 14.0), encoded_marker(value >> (part * 12)))
        first, past = snapshot["first"], snapshot["past"]
        viewport = snapshot[LIT_RECTANGLES]["viewport"]
        bounded = snapshot[LIT_LABEL_READS] <= 2
        geometry_matches = True
        if snapshot[LIT_OPEN]:
            bounded = first <= past <= snapshot[LIT_COUNT] and snapshot[LIT_LABEL_READS] <= max(0, past - first) + 3
            row_height = snapshot[LIT_RECTANGLES]["first_row"][3]
            bounded = bounded and (past == first == 0 if snapshot[LIT_COUNT] == 0 else row_height > 0.0 and past - first <= math.ceil(viewport[3] / row_height) + 2)
            bx, by, bw, bh = snapshot[LIT_RECTANGLES]["popup"]
            tx, ty, tw, th = snapshot[LIT_RECTANGLES][LIT_TRIGGER]
            logical_width, logical_height = snapshot[LIT_LOGICAL_EXTENT]
            geometry_matches = 0.0 <= bx <= logical_width - bw and 0.0 <= by <= logical_height - bh
            if not self.searchable and snapshot["bottom"]:
                geometry_matches = geometry_matches and by + bh <= ty + 0.75
            else:
                geometry_matches = geometry_matches and by >= ty + th - 0.75
            if bw > 12.0 and bh > 12.0:
                probe("popup_skin_gutter", (bx + 4.0, by + bh / 2.0), (26, 34, 45), 8)
            lx, ly, lw, lh = snapshot[LIT_RECTANGLES]["list"]
            if lw > 12.0 and lh > 12.0:
                probe("list_skin_gutter", (lx + 4.0, ly + lh / 2.0), (17, 23, 31), 8)
            sx, sy, sw, sh = snapshot[LIT_RECTANGLES]["thumb"]
            track_x, track_y, track_w, track_h = snapshot[LIT_RECTANGLES]["track"]
            if track_w > 0.0 and track_h > sh + 8.0:
                free_top, free_bottom = sy - track_y, track_y + track_h - sy - sh
                clear_y = track_y + free_top / 2.0 if free_top > free_bottom else sy + sh + free_bottom / 2.0
                probe("scroll_track_skin", (track_x + track_w / 2.0, clear_y), (39, 63, 34) if skin == LIT_ALTERNATE else (24, 29, 37), 8)
                probe("scroll_thumb_skin", (sx + sw / 2.0, sy + sh / 2.0), (49, 100, 63) if skin == LIT_ALTERNATE else (45, 60, 80), 8)
            rx, ry, rw, rh = snapshot[LIT_RECTANGLES]["cursor_row"]
            if rw > 40.0 and rh > 8.0 and viewport[1] + 4.0 <= ry + rh / 2.0 <= viewport[1] + viewport[3] - 4.0:
                row_color = (38, 80, 123) if snapshot["cursor"] == snapshot["selected"] else ((49, 100, 63) if skin == LIT_ALTERNATE else (45, 60, 80))
                probe("preview_row_committed_skin", (rx + rw - 20.0, ry + rh / 2.0), row_color, 8)
        if self.searchable or snapshot["visible"]:
            tx, ty, tw, th = snapshot[LIT_RECTANGLES][LIT_TRIGGER]
            if tw > 100.0 and th > 16.0:
                if not snapshot["enabled"]:
                    field_color = (54, 60, 42) if skin == LIT_ALTERNATE else (37, 43, 52)
                elif snapshot[LIT_OPEN]:
                    field_color = (36, 76, 48) if skin == LIT_ALTERNATE else (33, 47, 65)
                elif snapshot["focused"]:
                    field_color = (18, 26, 37)
                else:
                    field_color = (49, 100, 63) if skin == LIT_ALTERNATE else (45, 60, 80)
                probe("combo_trigger_skin", (tx + tw - 56.0, ty + th / 2.0), field_color, 8)
        if self.searchable and snapshot[LIT_OPEN]:
            qx, qy, qw, qh = snapshot[LIT_RECTANGLES]["query"]
            content = snapshot[LIT_RECTANGLES]["query_content"]
            geometry_matches = geometry_matches and qw > 0.0 and qh == 36.0 and content[2] > 0.0
            background = (18, 26, 37) if snapshot[LIT_QUERY_FOCUSED] else (15, 20, 28)
            if qw > 32.0 and qh > 12.0:
                probe("search_query_edit_skin", (qx + qw - 16.0, qy + qh - 6.0), background, 8)
            if snapshot[LIT_QUERY_FOCUSED] and snapshot["query_anchor"] != snapshot["query_caret"]:
                sx, sy, sw, sh = snapshot[LIT_RECTANGLES]["query_selection"]
                geometry_matches = geometry_matches and sw > 1.0 and sh > 1.0
                if sw > 1.0 and sh > 1.0:
                    linear_background = linear_channels(background)
                    color = tuple(foreground * 0.75 + backdrop * 0.25 for foreground, backdrop in zip((0.20, 0.40, 0.78), linear_background))
                    probe("search_query_selection", (sx + min(sw / 2.0, 4.0), min(sy + sh, content[1] + content[3]) - 2.0), linear_rgb_bytes(color))
        model_matches = snapshot[LIT_SKIN] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
        return {"extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "bounded": bounded, "geometry_matches": geometry_matches,
            LIT_PASSED: model_matches and bounded and geometry_matches and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot)) and all(item[LIT_PASSED] for item in probes)}
