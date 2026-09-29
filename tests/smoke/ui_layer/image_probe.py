"""Decode passive image snapshots and qualify sprite, nine-slice, tint and clip pixels."""
from __future__ import annotations

import math
import re

from interaction_smoke import linear_rgb_bytes
from radio_group_probe import sampled_tile
from slider_probe import compose, encoded, sampled_region
from window_capture_smoke import SmokeFailure


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiImageSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiImageSmoke: state sequence=(\d+) values=((?:\d+,){9}\d+)")
GEOMETRY = re.compile(rf"UiImageSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiImageSmoke: skin=(default|alternate)")
FIELDS = (
    "phase", "focus_code", "before_clicks", "after_clicks", "parent", "child", "popup_count", "image_targets",
    "declared_region", "post_declaration_region",
)
RECTANGLES = (
    "before", "after", "natural_sprite", "natural_slice", "fixed_sprite", "fixed_slice", "stretch", "tinted",
    "transparent", "frozen", "external", "external_clip", "parent_image", "child_image", "parent", "child",
)
FILL = ((53, 131, 192), (97, 176, 233))
MARK = (235, 245, 255)
WHITE = (1.0, 1.0, 1.0, 1.0)
SENTINEL = linear_rgb_bytes((0.07, 0.13, 0.2))


def snapshot_from_logs(text):
    states, displays, skins = list(STATE.finditer(text)), list(DISPLAY.finditer(text)), list(SKIN.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence, values = int(state[1]), tuple(map(int, state[2].split(",")))
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in GEOMETRY.finditer(text)
        if int(match[1]) == sequence}
    if any(name not in rectangles for name in RECTANGLES):
        return None
    display = tuple(map(float, displays[-1].groups()))
    if not all(math.isfinite(value) for value in (*display, *(v for r in rectangles.values() for v in r))):
        raise SmokeFailure("image fixture reported nonfinite display or geometry")
    if any(value <= 0.0 for value in display) or any(r[2] < 0.0 or r[3] < 0.0 for r in rectangles.values()):
        raise SmokeFailure("image fixture reported invalid display or placement extents")
    if any(value > 0xFFFFFF for value in (*values, sequence)):
        raise SmokeFailure("image fixture exceeds its two-part displayed marker schema")
    return {
        "sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(FIELDS, values)),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles,
    }


def center(rectangle):
    return rectangle[0] + rectangle[2] / 2.0, rectangle[1] + rectangle[3] / 2.0


def observe_image(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale, rectangles = snapshot["scale"], snapshot["rectangles"]
    probes = []
    geometry_matches = snapshot["phase"] in (0, 1) and snapshot["image_targets"] == 0

    def pixel(point):
        return round(point[0] * scale[0]), round(point[1] * scale[1])

    def inside(rectangle, position):
        x, y = (position[0] + 0.5) / scale[0], (position[1] + 0.5) / scale[1]
        return rectangle[0] <= x < rectangle[0] + rectangle[2] and rectangle[1] <= y < rectangle[1] + rectangle[3]

    def record(name, position, color, tolerance=8):
        x, y = position
        if not 0 <= x < width or not 0 <= y < height:
            raise SmokeFailure(f"image probe '{name}' falls outside {width}x{height}: ({x},{y})")
        observed = rows[y][x]
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": list(position), "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance})

    for index, value in enumerate((*snapshot["values"], snapshot["sequence"])):
        for part in range(2):
            marker = index * 2 + part
            record(f"model_{marker}", pixel((16.0 + 12.0 * marker, snapshot["logical_extent"][1] - 14.0)),
                encoded(value >> (12 * part)), 5)
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    logical_width = snapshot["logical_extent"][0]
    main_width = max(300.0, logical_width * 0.5 - 40.0)
    right = 24.0 + main_width + 32.0
    other_width = max(220.0, logical_width - right - 24.0)
    declarations = {
        "before": (32.0, 56.0, main_width - 16.0, 28.0),
        "after": (32.0, 372.0, main_width - 16.0, 28.0),
        "natural_sprite": (32.0, 92.0, 24.0, 24.0),
        "natural_slice": (32.0, 124.0, 24.0, 24.0),
        "fixed_sprite": (32.0, 156.0, 96.0, 40.0),
        "fixed_slice": (32.0, 204.0, 96.0, 40.0),
        "stretch": (32.0, 252.0, main_width - 16.0, 32.0),
        "tinted": (32.0, 292.0, 96.0, 40.0),
        "transparent": (32.0, 340.0, 96.0, 24.0),
        "frozen": (right + 8.0, 56.0, 96.0, 40.0),
        "external": (right + 8.0, 184.0, other_width - 16.0, 40.0),
        "external_clip": (right + 8.0, 184.0, (other_width - 16.0) * 0.5, 40.0),
    }
    geometry_matches = geometry_matches and all(all(abs(actual - wanted) <= 0.025
        for actual, wanted in zip(rectangles[name], declaration)) for name, declaration in declarations.items())

    def image(name, kind, tint, backdrop, constraint=None, obscured=None):
        nonlocal geometry_matches
        bounds = rectangles[name]
        bx, by, bw, bh = bounds
        geometry_matches = geometry_matches and bw > 0.0 and bh > 0.0 and bx >= 0.0 and by >= 0.0 \
            and bx + bw <= snapshot["logical_extent"][0] + 0.025 and by + bh <= snapshot["logical_extent"][1] + 0.025

        def color_at(position):
            if constraint is not None and not inside(constraint, position):
                return SENTINEL
            if kind == "dot":
                source, alpha = sampled_tile("dot", bounds, position, scale, MARK, MARK)
            else:
                source, alpha = sampled_region(bounds, position, scale, *FILL)
            return compose(source, alpha, tint, backdrop)

        def sample(label, point):
            position = pixel(point)
            if inside(bounds, position) and (obscured is None or not inside(obscured, position)):
                record(f"{name}_{label}", position, color_at(position))

        sample("middle", center(bounds))
        if kind == "dot":
            sample("off_center_dot", (bx + bw * 0.60, by + bh * 0.35))
            sample("transparent_corner", (bx + bw * 0.05, by + bh * 0.05))
        else:
            sample("left_fill", (bx + min(8.0, bw / 2.0), by + bh / 2.0))
            sample("left_border", (bx + 1.0, by + bh / 2.0))
            sample("right_border", (bx + bw - 1.0, by + bh / 2.0))
        if constraint is not None:
            cx, cy, cw, ch = constraint
            geometry_matches = geometry_matches and cw > 0.0 and ch > 0.0 \
                and bx <= cx and by <= cy and cx + cw <= bx + bw and cy + ch <= by + bh
            sample("inside_clip", center(constraint))
            for side, point in (("left", (cx - 2.0, cy + ch / 2.0)),
                    ("right", (cx + cw + 2.0, cy + ch / 2.0))):
                position = pixel(point)
                if inside(bounds, position):
                    record(f"{name}_outside_clip_{side}", position, SENTINEL)

    for name, kind in (("natural_sprite", "dot"), ("natural_slice", "slice"),
            ("fixed_sprite", "dot"), ("fixed_slice", "slice"), ("stretch", "slice")):
        image(name, kind, WHITE, panel)
    image("tinted", "dot", (0.4, 0.7, 0.9, 0.5), panel)
    image("transparent", "dot", (1.0, 1.0, 1.0, 0.0), panel)
    phase = snapshot["phase"]
    image("frozen", "slice" if phase == 0 else "dot", WHITE if phase == 0 else (0.5, 0.75, 1.0, 0.5), panel)
    image("external", "slice", WHITE, panel, constraint=rectangles["external_clip"])
    natural = rectangles["natural_sprite"]
    record("replacement_panel_gutter", pixel((natural[0] + natural[2] + 8.0, natural[1] + natural[3] / 2.0)), panel)
    for opened, name, kind, tint in (("parent", "parent_image", "slice", WHITE),
            ("child", "child_image", "dot", (0.5, 1.0, 0.5, 0.75))):
        if snapshot[opened]:
            popup = rectangles[opened]
            declaration = popup[0] + 8.0, popup[1] + (44.0 if opened == "parent" else 8.0), popup[2] - 16.0, 40.0
            geometry_matches = geometry_matches and all(abs(actual - wanted) <= 0.025
                for actual, wanted in zip(rectangles[name], declaration))
            obscured = rectangles["child"] if opened == "parent" and snapshot["child"] else None
            image(name, kind, tint, (26, 34, 45), obscured=obscured)
        else:
            geometry_matches = geometry_matches and rectangles[name][2:] == (0.0, 0.0) \
                and rectangles[opened][2:] == (0.0, 0.0)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    model_matches = model_matches and snapshot["declared_region"] == (1 if phase == 0 else 2) \
        and snapshot["post_declaration_region"] == (2 if phase == 0 else 1)
    return {
        "extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "geometry_matches": geometry_matches, "passed": model_matches and geometry_matches
            and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot))
            and all(probe["passed"] for probe in probes),
    }
