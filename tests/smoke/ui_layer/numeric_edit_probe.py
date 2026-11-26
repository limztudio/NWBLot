from __future__ import annotations

import math
import struct

from edit_probe import caret_pixel_column
from probe_common import FrameProbeRecorder, compile_smoke_patterns, ink_coverage, snapshot_from_patterns, text_hash
from window_capture_smoke import SmokeFailure
from probe_reference import linear_channels, linear_rgb_bytes

LIT_CLIPBOARD_FOCUS = "clipboard_focus"
LIT_ENABLED = "enabled"
LIT_READONLY = "readonly"
LIT_COHERENT = "coherent"
LIT_UTF_8 = "utf-8"
LIT_SEQUENCE = "sequence"
LIT_SKIN = "skin"
LIT_VALUES = "values"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_SCALE = "scale"
LIT_RECTANGLES = "rectangles"
LIT_NAME = "name"
LIT_POSITION = "position"
LIT_EXPECTED = "expected"
LIT_OBSERVED = "observed"
LIT_ERROR = "error"
LIT_PASSED = "passed"
LIT_CLIPBOARD = "clipboard"
LIT_COVERAGE = "coverage"

FIELDS = ("integer_bits", "float_bits", "i_bytes", "i_hash", "i_anchor", "i_caret", "i_status", "i_dirty", "i_focus", "f_bytes", "f_hash", "f_anchor", "f_caret", "f_status", "f_dirty", "f_focus", "clipboard_bytes", "clipboard_hash",
    "clipboard_anchor", "clipboard_caret", LIT_CLIPBOARD_FOCUS, LIT_ENABLED, LIT_READONLY, "clamp", "commits", "cancels", "rejects", "clamps", "restored", LIT_COHERENT)
DISPLAY, STATE, GEOMETRY, SKIN = compile_smoke_patterns("UiNumericEditSmoke", len(FIELDS))
RECTANGLES = tuple(f"{field}_{part}" for field in ("integer", "float", "clipboard") for part in ("bounds", "content", "caret", "selection"))
MARKER_PARTS = tuple(6 if index < 2 else 3 if index in (3, 10, 17) else 2 for index in range(len(FIELDS))) + (2,)
MARKER_TOLERANCE = 5
UINT64_MASK = 0xFFFFFFFFFFFFFFFF


def integer_bits(value):
    return value & UINT64_MASK


def float_bits(value):
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def draft_fields(prefix, text, *, anchor=None, caret=None, status=None, dirty=None):
    size = len(text.encode(LIT_UTF_8))
    fields = {f"{prefix}_bytes": size, f"{prefix}_hash": text_hash(text)}
    if anchor is not None:
        fields[f"{prefix}_anchor"] = anchor
    if caret is not None:
        fields[f"{prefix}_caret"] = caret
    if status is not None:
        fields[f"{prefix}_status"] = status
    if dirty is not None:
        fields[f"{prefix}_dirty"] = int(dirty)
    return fields


def snapshot_from_logs(text):
    return snapshot_from_patterns(text, STATE, DISPLAY, SKIN, GEOMETRY, FIELDS, RECTANGLES, fixture="numeric", max_value=UINT64_MASK)


def observe_numeric_edit(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale_x, scale_y = snapshot[LIT_SCALE]
    recorder = FrameProbeRecorder(frame, snapshot[LIT_SCALE], "numeric")
    probes = recorder.probes
    probe = lambda name, position, color, tolerance=MARKER_TOLERANCE: recorder.probe(name, position, color, tolerance)

    marker = recorder.probe_markers((*snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE]), 11.0, 8.0, snapshot[LIT_LOGICAL_EXTENT][1] - 13.0, parts=MARKER_PARTS, tolerance=MARKER_TOLERANCE)
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    probe("panel_gutter", (28.0, 330.0), panel, 8)
    geometry_matches = marker == 73 and snapshot[LIT_COHERENT] == 1
    for field, prefix in (("integer", "i"), ("float", "f"), (LIT_CLIPBOARD, LIT_CLIPBOARD)):
        bx, by, bw, bh = snapshot[LIT_RECTANGLES][f"{field}_bounds"]
        cx, cy, cw, ch = snapshot[LIT_RECTANGLES][f"{field}_content"]
        kx, ky, kw, kh = snapshot[LIT_RECTANGLES][f"{field}_caret"]
        sx, sy, sw, sh = snapshot[LIT_RECTANGLES][f"{field}_selection"]
        enabled = bool(snapshot[LIT_ENABLED]) or field == LIT_CLIPBOARD
        focused = bool(snapshot[f"{prefix}_focus"]) if prefix != LIT_CLIPBOARD else bool(snapshot[LIT_CLIPBOARD_FOCUS])
        geometry_matches = geometry_matches and abs(bw - 400.0) <= 0.75 and abs(bh - 40.0) <= 0.75
        geometry_matches = geometry_matches and cw > 0.0 and ch > 0.0 and kw > 0.0 and kh > 0.0
        geometry_matches = geometry_matches and cx >= bx and cy >= by and cx + cw <= bx + bw + 0.75
        geometry_matches = geometry_matches and cy + ch <= by + bh + 0.75 and cx <= kx <= cx + cw + 0.75
        background = (29, 33, 40) if not enabled else (18, 26, 37) if focused else (15, 20, 28)
        probe(f"{field}_edit_skin", (bx + bw - 16.0, by + bh - 6.0), background, 5 if focused else 8)
        probe(f"{field}_clip_gutter", (bx + bw + 4.0, by + bh / 2.0), panel, 8)
        anchor, caret = snapshot[f"{prefix}_anchor"], snapshot[f"{prefix}_caret"]
        if field != LIT_CLIPBOARD and snapshot[f"{prefix}_bytes"] and anchor == caret:
            foreground = (0.92, 0.94, 0.98) if enabled else (0.48, 0.50, 0.55)
            caret_column = caret_pixel_column(kx, scale_x)
            left, right = max(0, math.ceil(cx * scale_x)), min(width, math.floor((cx + min(cw, 200.0)) * scale_x))
            top, bottom = max(0, math.ceil(cy * scale_y)), min(height, math.floor((cy + ch) * scale_y))
            candidates = []
            coverage_cache = {}
            for y in range(top, bottom):
                for x in range(left, right):
                    if abs(x - caret_column) <= 2:
                        continue
                    pixel = rows[y][x]
                    covered = coverage_cache.get(pixel)
                    if covered is None:
                        covered = ink_coverage(pixel, background, foreground)
                        coverage_cache[pixel] = covered
                    coverage, residual, color = covered
                    if coverage >= 0.25:
                        candidates.append((residual, -coverage, x, y, color, rows[y][x]))
            closest = min(candidates, default=None)
            if closest is None:
                probes.append({LIT_NAME: f"{field}_text_ink", LIT_COVERAGE: 0.0, LIT_PASSED: False})
            else:
                error, negative_coverage, x, y, color, observed = closest
                probes.append({LIT_NAME: f"{field}_text_ink", LIT_POSITION: [x, y], LIT_EXPECTED: list(color), LIT_OBSERVED: list(observed), LIT_COVERAGE: -negative_coverage, LIT_ERROR: error, LIT_PASSED: error <= 5})
        if focused and anchor != caret:
            geometry_matches = geometry_matches and sw > 1.0 and sh > 1.0
            if sw > 1.0 and sh > 1.0:
                linear_background = linear_channels(background)
                color = tuple(foreground * 0.75 + backdrop * 0.25 for foreground, backdrop in zip((0.20, 0.40, 0.78), linear_background))
                probe(f"{field}_selection", (sx + min(sw / 2.0, 4.0), min(sy + sh, cy + ch) - 2.0), linear_rgb_bytes(color))
        elif focused and enabled and (field == LIT_CLIPBOARD or not snapshot[LIT_READONLY]):
            geometry_matches = geometry_matches and abs(kw * scale_x - 1.0) <= 0.01
            column = caret_pixel_column(kx, scale_x)
            row = round((min(ky + kh, cy + ch) - 0.75) * scale_y)
            if not 0 <= column < width or not 0 <= row < height:
                raise SmokeFailure(f"numeric caret falls outside {width}x{height}: ({column},{row})")
            color = linear_rgb_bytes((0.95, 0.97, 1.0))
            pixels = [rows[py][column] for py in range(max(0, row - 1), min(height, row + 2))]
            error = min(max(abs(actual - reference) for actual, reference in zip(pixel, color)) for pixel in pixels)
            probes.append({LIT_NAME: f"{field}_caret", LIT_POSITION: [column, row], LIT_EXPECTED: list(color), LIT_OBSERVED: [list(pixel) for pixel in pixels], LIT_ERROR: error, LIT_PASSED: error <= MARKER_TOLERANCE})
    model_matches = snapshot[LIT_SKIN] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    extra_matches = extra is None or extra(snapshot)
    return {"extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "geometry_matches": geometry_matches, "model_matches": model_matches, "extra_matches": extra_matches,
        "marker_count": marker, LIT_PASSED: model_matches and geometry_matches and extra_matches and (extent is None or (width, height) == extent) and all(item[LIT_PASSED] for item in probes)}
