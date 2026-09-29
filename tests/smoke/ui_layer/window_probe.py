"""Read window fixture metadata and check geometry/model pixels in real captures."""
from __future__ import annotations

import math
import re

from interaction_smoke import linear_rgb_bytes
from window_capture_smoke import SmokeFailure

NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiWindowSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(rf"UiWindowSmoke: state sequence=(\d+) bounds=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})"
    r" collapsed=([01]) locked=([01]) count=(\d+)")
ACTION = re.compile(r"UiWindowSmoke: action=(increase|locked) count=(\d+) locked=([01]) actions=(\d+)")
RECTANGLE = re.compile(rf"(title|collapse|resize|button|lock|content)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
MINIMUM = re.compile(rf"minimum=({NUMBER}),({NUMBER})")
TITLE_ANCHOR = (0.70, 0.02, 0.35)
CONTENT_ANCHOR = (0.02, 0.30, 0.75)
TOLERANCE = 4
SKIN_TOLERANCE = 8
# Authored sRGB centers are independent of the fixture generator and application marker colors.
SKIN_COLORS = {
    "default": {"title": (34, 47, 64), "body": (27, 34, 44), "title_height": 40.0},
    "alternate": {"title": (142, 92, 28), "body": (45, 72, 38), "title_height": 36.0},
}
SKIN = re.compile(r"UiWindowSmoke: skin=(default|alternate)")


def snapshot_from_logs(text):
    states = list(STATE.finditer(text))
    displays = list(DISPLAY.finditer(text))
    skins = list(SKIN.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence = int(state[1])
    rectangles = {}
    minimum = None
    for line in text.splitlines():
        if f"UiWindowSmoke: geometry sequence={sequence} " in line or f"UiWindowSmoke: controls sequence={sequence} " in line:
            rectangles.update({match[1]: tuple(map(float, match.groups()[1:])) for match in RECTANGLE.finditer(line)})
            match = MINIMUM.search(line)
            if match:
                minimum = tuple(map(float, match.groups()))
    if len(rectangles) != 6 or minimum is None:
        return None
    display = tuple(map(float, displays[-1].groups()))
    values = (*display, *map(float, state.groups()[1:5]), *minimum,
        *(value for rectangle in rectangles.values() for value in rectangle))
    if not all(math.isfinite(value) for value in values) or not all(value > 0.0 for value in display):
        raise SmokeFailure("window fixture reported invalid geometry or display metrics")
    return {"skin": skins[-1][1], "sequence": sequence, "bounds": tuple(map(float, state.groups()[1:5])),
        "collapsed": bool(int(state[6])), "locked": bool(int(state[7])), "count": int(state[8]),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles, "minimum": minimum}


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def encoded(value):
    integer = math.floor(max(0.0, value) + 0.5)
    return linear_rgb_bytes(tuple(((integer >> shift) & 15) / 15.0 for shift in (0, 4, 8)))


def observe_window(frame, snapshot, expected_bounds, count, collapsed, locked, *, extent=None, skin="default"):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    probes = []

    def probe(name, logical_position, expected=None, *, colored=False, tolerance=TOLERANCE):
        x = round(logical_position[0] * scale_x)
        y = round(logical_position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"window probe '{name}' falls outside {width}x{height}: ({x},{y})")
        sample = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in sample)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, expected)) if expected else None
        probes.append({"name": name, "position": [x, y], "expected": list(expected) if expected else "skin color",
            "observed": list(observed), "error": error,
            "passed": max(observed) > 12 if colored else error <= tolerance})

    values = (*snapshot["bounds"], snapshot["count"], int(snapshot["collapsed"]), int(snapshot["locked"]), snapshot["sequence"])
    logical_height = height / scale_y
    for index, value in enumerate(values):
        probe(f"model_{index}", (19.0 + index * 20.0, logical_height - 14.0), encoded(value))
    x, y, window_width, window_height = expected_bounds
    title_height = snapshot["rectangles"]["title"][3]
    visible_height = title_height if collapsed else window_height
    probe("title_anchor", (x + window_width - 9.0, y + 9.0), linear_rgb_bytes(TITLE_ANCHOR))
    probe("content_visibility", (x + window_width - 9.0, y + title_height + 9.0),
        (0, 0, 0) if collapsed else linear_rgb_bytes(CONTENT_ANCHOR))
    probe("title_skin", (x + window_width - 36.0, y + title_height / 2.0),
        SKIN_COLORS[skin]["title"], tolerance=SKIN_TOLERANCE)
    if not collapsed:
        probe("body_skin", (x + window_width - 36.0, y + window_height - 8.0),
            SKIN_COLORS[skin]["body"], tolerance=SKIN_TOLERANCE)
    probe("painted_title_edge", (x + 3.0, y + title_height / 2.0), colored=True)
    probe("painted_bottom_edge", (x + window_width - 3.0, y + visible_height - 3.0), colored=True)
    probe("clear_right", (x + window_width + 3.0, y + 6.0), (0, 0, 0))
    probe("clear_below", (x + window_width / 2.0, y + visible_height + 3.0), (0, 0, 0))
    model_matches = max(abs(actual - expected) for actual, expected in zip(snapshot["bounds"], expected_bounds)) <= 0.75
    model_matches = model_matches and snapshot["skin"] == skin and title_height == SKIN_COLORS[skin]["title_height"]
    model_matches = model_matches and snapshot["count"] == count and snapshot["collapsed"] == collapsed and snapshot["locked"] == locked
    return {"skin": skin, "extent": [width, height], "snapshot": snapshot, "expected_bounds": list(expected_bounds),
        "count": count, "collapsed": collapsed, "locked": locked, "probes": probes,
        "passed": model_matches and (extent is None or (width, height) == extent) and all(item["passed"] for item in probes)}
