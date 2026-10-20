"""Decode post-end slider snapshots and qualify their exact value and atlas pixels."""
from __future__ import annotations

import math
import re
import struct

from window_capture_smoke import SmokeFailure
from probe_common import center, snapshot_from_patterns
from probe_reference import compose, encoded_marker, linear_channels, sampled_region

LIT_VALUES = "values"
LIT_SEQUENCE = "sequence"
LIT_RECTANGLES = "rectangles"
LIT_BITS = "bits"
LIT_NAME = "name"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_RANGE_CODE = "range_code"
LIT_HOVER = "hover"
LIT_PARENT = "parent"
STRUCT_UINT64 = "<Q"
STRUCT_DOUBLE = "<d"
STRUCT_FLOAT = "<f"


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiSliderSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiSliderSmoke: state sequence=(\d+) values=((?:\d+,){15}\d+) bits=((?:\d+,){3}\d+)")
GEOMETRY = re.compile(rf"UiSliderSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiSliderSmoke: skin=(default|alternate)")
FIELDS = ("focus_code", "changes", "popup_changes", "before_clicks", "after_clicks", "enabled", "range_code", "step_code", "parent", "popup_count", "focus_scope", "main_valid", "main_dragging", "popup_valid", "popup_dragging", "external_intents",)
BIT_FIELDS = ("main_bits", "disabled_bits", "constant_bits", "popup_bits")
PREFIXES = ("main", "disabled", "constant", "popup")
PARTS = ("bounds", "clip", "travel", "track", "center", "thumb")
RECTANGLES = ("before", "after", "open", "parent", "popup_close") + tuple(f"{prefix}_{part}" for prefix in PREFIXES for part in PARTS)
TRACK = ((20, 29, 41), (63, 80, 101))
NORMAL = ((69, 131, 185), (130, 193, 238))
HOVER = ((86, 164, 221), (166, 221, 255))
MARKER_TOLERANCE = 5
SKIN_TOLERANCE = 8


def value_bits(value):
    return struct.unpack(STRUCT_UINT64, struct.pack(STRUCT_DOUBLE, value))[0]


def bits_value(value):
    return struct.unpack(STRUCT_DOUBLE, struct.pack(STRUCT_UINT64, value))[0]


def f32(value):
    return struct.unpack(STRUCT_FLOAT, struct.pack(STRUCT_FLOAT, value))[0]


def snapshot_from_logs(text):
    snapshot = snapshot_from_patterns(text, STATE, DISPLAY, SKIN, GEOMETRY, FIELDS, RECTANGLES, fixture="slider", bit_fields=BIT_FIELDS)
    if snapshot is None:
        return None
    values, sequence, rectangles = snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE], snapshot[LIT_RECTANGLES]
    bits = snapshot[LIT_BITS]
    if any(rectangle[2] < 0.0 or rectangle[3] < 0.0 for rectangle in rectangles.values()):
        raise SmokeFailure("slider fixture reported a negative placement extent")
    if any(value > 0xFFFFFF for value in (*values, sequence)) or any(value > 0xFFFFFFFFFFFFFFFF for value in bits):
        raise SmokeFailure("slider fixture exceeds its displayed marker schema")
    if not all(math.isfinite(bits_value(value)) for value in bits):
        raise SmokeFailure("slider fixture reported a nonfinite typed value")
    return snapshot


def observe_slider(frame, snapshot, expected, *, extent=None, skin="default", extra=None, modes=None):
    width, height, rows = frame
    scale = snapshot["scale"]
    rectangles = snapshot[LIT_RECTANGLES]
    modes = modes or {}
    probes = []

    def record(name, pixel, color, tolerance=SKIN_TOLERANCE):
        x, y = pixel
        if not 0 <= x < width or not 0 <= y < height:
            raise SmokeFailure(f"slider probe '{name}' falls outside {width}x{height}: ({x},{y})")
        observed = rows[y][x]
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({ LIT_NAME: name, "position": [x, y], LIT_EXPECTED: list(color), "observed": list(observed), "error": error, LIT_PASSED: error <= tolerance, })

    def pixel(point):
        return round(point[0] * scale[0]), round(point[1] * scale[1])

    marker = 0
    for value, parts in (*((value, 2) for value in snapshot[LIT_VALUES]), *((value, 6) for value in snapshot[LIT_BITS]), (snapshot[LIT_SEQUENCE], 2)):
        for part in range(parts):
            record(f"model_{marker}", pixel((16.0 + 12.0 * marker, snapshot[LIT_LOGICAL_EXTENT][1] - 14.0)), encoded_marker(value >> (part * 12)), MARKER_TOLERANCE)
            marker += 1
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    bounds = rectangles["main_bounds"]
    record("replacement_panel_gutter", pixel((bounds[0] + bounds[2] - 8.0, bounds[1] + bounds[3] + 2.0)), panel)
    geometry_matches = snapshot[LIT_RANGE_CODE] in (0, 1) and snapshot["step_code"] in (0, 1)

    def close(left, right):
        return abs(left - right) <= 0.025

    def same_rectangle(actual, wanted):
        return all(close(left, right) for left, right in zip(actual, wanted))

    def contains(outer, inner):
        ox, oy, ow, oh = outer
        ix, iy, iw, ih = inner
        return iw >= 0.0 and ih >= 0.0 and ix >= ox - 0.025 and iy >= oy - 0.025 \
            and ix + iw <= ox + ow + 0.025 and iy + ih <= oy + oh + 0.025

    def control(prefix, normalized, enabled, backdrop):
        nonlocal geometry_matches
        bounds, clip, travel, track, centers, thumb = (rectangles[f"{prefix}_{part}"] for part in PARTS)
        geometry_matches = geometry_matches and contains((0.0, 0.0, *snapshot[LIT_LOGICAL_EXTENT]), bounds)
        geometry_matches = geometry_matches and bounds[2] > 32.0 and close(bounds[3], 32.0)
        geometry_matches = geometry_matches and contains(bounds, clip) and contains(bounds, travel) and contains(travel, thumb)
        bx, by, bw, bh = bounds
        geometry_matches = geometry_matches and same_rectangle(travel, (bx + 4.0, by + 4.0, bw - 8.0, bh - 8.0))
        geometry_matches = geometry_matches and same_rectangle(centers, (bx + 16.0, by + 4.0, bw - 32.0, bh - 8.0))
        geometry_matches = geometry_matches and same_rectangle(track, (centers[0], by + 10.0, centers[2], 12.0))
        geometry_matches = geometry_matches and close(thumb[2], 24.0) and close(thumb[3], 24.0)
        geometry_matches = geometry_matches and close(center(thumb)[0], centers[0] + normalized * centers[2])
        geometry_matches = geometry_matches and close(center(thumb)[1], center(bounds)[1])
        mode = modes.get(prefix, "normal")
        tint = (0.55, 0.55, 0.55, 0.6) if not enabled else ((0.85, 0.85, 0.85, 1.0) if mode == "pressed" else ((1.08, 1.08, 1.08, 1.0) if mode == LIT_HOVER else (1.0, 1.0, 1.0, 1.0)))
        track_tint = (0.55, 0.55, 0.55, 0.6) if not enabled else (1.0, 1.0, 1.0, 1.0)
        fill, border = HOVER if enabled and mode == LIT_HOVER else NORMAL
        # Pick the track half opposite the thumb, inside the opaque middle slice.
        track_point = (track[0] + track[2] * (0.75 if normalized < 0.5 else 0.25), center(track)[1])
        record(f"{prefix}_track_fill", pixel(track_point), compose(linear_channels(TRACK[0]), 1.0, track_tint, backdrop))
        thumb_point = center(thumb)
        thumb_pixel = pixel(thumb_point)

        def background_at(position):
            source, alpha = sampled_region(track, position, scale, *TRACK)
            return compose(source, alpha, track_tint, backdrop)

        source, alpha = sampled_region(thumb, thumb_pixel, scale, fill, border)
        record(f"{prefix}_thumb_fill", thumb_pixel, compose(source, alpha, tint, background_at(thumb_pixel)))
        candidates = []
        tx, ty, tw, th = thumb
        left, right = max(0, math.floor((tx + tw - 1.5) * scale[0])), min(width, math.ceil((tx + tw) * scale[0]))
        sample_y = round((ty + th / 2.0) * scale[1])
        for sample_x in range(left, right) if 0 <= sample_y < height else ():
            position = sample_x, sample_y
            source, alpha = sampled_region(thumb, position, scale, fill, border)
            if alpha < 0.7:
                continue
            color = compose(source, alpha, tint, background_at(position))
            observed = rows[sample_y][sample_x]
            error = max(abs(actual - reference) for actual, reference in zip(observed, color))
            candidates.append((error, position, color))
        if candidates:
            _, position, color = min(candidates)
            record(f"{prefix}_thumb_border", position, color)
        else:
            probes.append({LIT_NAME: f"{prefix}_thumb_border", LIT_PASSED: False})

    main_maximum = 0.5 if snapshot[LIT_RANGE_CODE] else 1.0
    control("main", min(1.0, max(0.0, bits_value(snapshot["main_bits"]) / main_maximum)), bool(snapshot["enabled"]), panel)
    control("disabled", 0.75, False, panel)
    control("constant", 0.0, False, panel)
    if snapshot[LIT_PARENT]:
        popup = rectangles[LIT_PARENT]
        geometry_matches = geometry_matches and contains((0.0, 0.0, *snapshot[LIT_LOGICAL_EXTENT]), popup)
        control("popup", bits_value(snapshot["popup_bits"]), True, (26, 34, 45))
    else:
        for name in (LIT_PARENT, "popup_close", *(f"popup_{part}" for part in PARTS)):
            geometry_matches = geometry_matches and rectangles[name][2:] == (0.0, 0.0)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return { "extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "geometry_matches": geometry_matches, LIT_PASSED: model_matches and geometry_matches
            and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot)) and all(probe[LIT_PASSED] for probe in probes), }
