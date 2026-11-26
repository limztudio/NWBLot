from __future__ import annotations

import math

from probe_common import FrameProbeRecorder, MARKER_TOLERANCE, compile_smoke_patterns, snapshot_from_patterns
from probe_reference import linear_channels, linear_rgb_bytes

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


class ComboProbe:
    def __init__(self, prefix, fields, rectangles, *, searchable=False):
        self.fields = fields
        self.rectangles = rectangles
        self.searchable = searchable
        self.marker_origin = 16.0 if searchable else 18.0
        self.marker_stride = 12.0 if searchable else 16.0
        self.display, self.state, self.geometry, self.skin = compile_smoke_patterns(prefix, len(fields))

    def snapshot(self, text):
        return snapshot_from_patterns(text, self.state, self.display, self.skin, self.geometry, self.fields, self.rectangles, fixture="combo")

    def observe(self, frame, snapshot, expected, *, extent=None, skin="default", extra=None):
        width, height, _ = frame
        scale_x, scale_y = snapshot[LIT_SCALE]
        recorder = FrameProbeRecorder(frame, snapshot[LIT_SCALE], "combo")
        probes = recorder.probes
        probe = recorder.probe

        recorder.probe_markers((*snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE]), self.marker_origin, self.marker_stride, height / scale_y - 14.0, tolerance=MARKER_TOLERANCE)
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
