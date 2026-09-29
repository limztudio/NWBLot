"""Decode passive progress snapshots and qualify authored atlas pixels and clipping."""
from __future__ import annotations

import math
import re

from interaction_smoke import linear_rgb_bytes
from slider_probe import bits_value, compose, encoded, sampled_region, value_bits
from window_capture_smoke import SmokeFailure


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiProgressSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiProgressSmoke: state sequence=(\d+) values=((?:\d+,){7}\d+) bits=(\d+,\d+)")
GEOMETRY = re.compile(rf"UiProgressSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiProgressSmoke: skin=(default|alternate)")
FIELDS = ("phase", "focus_code", "before_clicks", "after_clicks", "parent", "child", "popup_count", "progress_targets")
BIT_FIELDS = ("declared_frozen_bits", "post_declaration_bits")
RECTANGLES = (
    "before", "after", "zero", "quarter", "full", "below", "above", "tiny", "frozen", "external", "external_clip",
    "parent_progress", "child_progress", "parent", "child",
)
FRACTIONS = {"zero": 0.0, "quarter": 0.25, "full": 1.0, "below": -2.0, "above": 3.0, "tiny": 0.01}
TRACK = ((20, 29, 41), (63, 80, 101))
FILL = ((53, 131, 192), (97, 176, 233))
WHITE = (1.0, 1.0, 1.0, 1.0)
SENTINEL = linear_rgb_bytes((0.07, 0.13, 0.2))


def snapshot_from_logs(text):
    states, displays, skins = list(STATE.finditer(text)), list(DISPLAY.finditer(text)), list(SKIN.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence, values, bits = int(state[1]), tuple(map(int, state[2].split(","))), tuple(map(int, state[3].split(",")))
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in GEOMETRY.finditer(text)
        if int(match[1]) == sequence}
    if any(name not in rectangles for name in RECTANGLES):
        return None
    display = tuple(map(float, displays[-1].groups()))
    if not all(math.isfinite(value) for value in (*display, *(v for r in rectangles.values() for v in r))):
        raise SmokeFailure("progress fixture reported nonfinite display or geometry")
    if any(value <= 0.0 for value in display) or any(r[2] < 0.0 or r[3] < 0.0 for r in rectangles.values()):
        raise SmokeFailure("progress fixture reported invalid display or placement extents")
    if any(value > 0xFFFFFF for value in (*values, sequence)) or any(value > 0xFFFFFFFFFFFFFFFF for value in bits):
        raise SmokeFailure("progress fixture exceeds its displayed marker schema")
    if not all(math.isfinite(bits_value(value)) for value in bits):
        raise SmokeFailure("progress fixture reported a nonfinite frozen value")
    return {
        "sequence": sequence, "skin": skins[-1][1], "values": values, "bits": bits, **dict(zip(FIELDS, values)),
        **dict(zip(BIT_FIELDS, bits)), "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles,
    }


def center(rectangle):
    return rectangle[0] + rectangle[2] / 2.0, rectangle[1] + rectangle[3] / 2.0


def observe_progress(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale, rectangles = snapshot["scale"], snapshot["rectangles"]
    probes = []
    geometry_matches = snapshot["phase"] in (0, 1) and snapshot["progress_targets"] == 0

    def pixel(point):
        return round(point[0] * scale[0]), round(point[1] * scale[1])

    def record(name, position, color, tolerance=8):
        x, y = position
        if not 0 <= x < width or not 0 <= y < height:
            raise SmokeFailure(f"progress probe '{name}' falls outside {width}x{height}: ({x},{y})")
        observed = rows[y][x]
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": list(position), "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance})

    def inside(rectangle, position):
        x, y = (position[0] + 0.5) / scale[0], (position[1] + 0.5) / scale[1]
        return rectangle[0] <= x < rectangle[0] + rectangle[2] and rectangle[1] <= y < rectangle[1] + rectangle[3]

    marker = 0
    for value, parts in (*((v, 2) for v in snapshot["values"]), *((v, 6) for v in snapshot["bits"]),
            (snapshot["sequence"], 2)):
        for part in range(parts):
            record(f"model_{marker}", pixel((16.0 + 12.0 * marker, snapshot["logical_extent"][1] - 14.0)),
                encoded(value >> (12 * part)), 5)
            marker += 1
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    logical_width = snapshot["logical_extent"][0]
    main_width = max(300.0, logical_width * 0.5 - 40.0)
    right = 24.0 + main_width + 32.0
    other_width = max(220.0, logical_width - right - 24.0)
    declarations = {name: (32.0, 92.0 + index * 40.0, main_width - 16.0, 32.0)
        for index, name in enumerate(FRACTIONS)}
    declarations.update({
        "before": (32.0, 56.0, main_width - 16.0, 28.0),
        "after": (32.0, 332.0, main_width - 16.0, 28.0),
        "frozen": (right + 8.0, 56.0, other_width - 16.0, 32.0),
        "external": (right + 8.0, 184.0, other_width - 16.0, 32.0),
        "external_clip": (right + 8.0, 184.0, (other_width - 16.0) * 0.5, 32.0),
    })
    geometry_matches = geometry_matches and all(all(abs(actual - wanted) <= 0.025
        for actual, wanted in zip(rectangles[name], declaration)) for name, declaration in declarations.items())

    def progress(name, fraction, backdrop, constraint=None, obscured=None):
        nonlocal geometry_matches
        bounds = rectangles[name]
        bx, by, bw, bh = bounds
        geometry_matches = geometry_matches and bw > 28.0 and abs(bh - 32.0) < 0.025
        geometry_matches = geometry_matches and bx >= 0.0 and by >= 0.0 \
            and bx + bw <= snapshot["logical_extent"][0] + 0.025 and by + bh <= snapshot["logical_extent"][1] + 0.025
        content = bx + 8.0, by + 8.0, bw - 16.0, bh - 16.0
        reveal_width = content[2] * min(1.0, max(0.0, fraction))
        reveal = content[0], content[1], reveal_width, content[3]
        canvas = content[0], content[1], max(12.0, reveal_width), content[3]

        def color_at(position):
            if constraint is not None and not inside(constraint, position):
                return SENTINEL
            source, alpha = sampled_region(bounds, position, scale, *TRACK)
            color = compose(source, alpha, WHITE, backdrop)
            if reveal_width > 0.0 and inside(reveal, position):
                source, alpha = sampled_region(canvas, position, scale, *FILL)
                color = compose(source, alpha, WHITE, color)
            return color

        def visible(position):
            return obscured is None or not inside(obscured, position)

        sample = pixel(center(content))
        if visible(sample):
            record(f"{name}_middle", sample, color_at(sample))
        if constraint is None:
            sample = pixel((bx + bw - 1.0, by + bh / 2.0))
            if visible(sample):
                record(f"{name}_track_border", sample, color_at(sample))
        if reveal_width > 0.0:
            sample = pixel((content[0] + min(reveal_width / 2.0, 8.0), content[1] + content[3] / 2.0))
            if visible(sample) and (constraint is None or inside(constraint, sample)):
                record(f"{name}_fill_left", sample, color_at(sample))
            sample = pixel((content[0] + reveal_width - 1.0, content[1] + content[3] / 2.0))
            if visible(sample) and inside(reveal, sample) and (constraint is None or inside(constraint, sample)):
                record(f"{name}_reveal_edge", sample, color_at(sample))
        if constraint is not None:
            cx, cy, cw, ch = constraint
            geometry_matches = geometry_matches and cw > 0.0 and ch > 0.0 \
                and bx <= cx and by <= cy and cx + cw <= bx + bw and cy + ch <= by + bh
            for side, point in (("left", (cx - 2.0, cy + ch / 2.0)),
                    ("right", (cx + cw + 2.0, cy + ch / 2.0))):
                sample = pixel(point)
                if inside(bounds, sample):
                    record(f"{name}_outside_clip_{side}", sample, SENTINEL)
            sample = pixel(center(constraint))
            record(f"{name}_inside_clip", sample, color_at(sample))

    for name, fraction in FRACTIONS.items():
        progress(name, fraction, panel)
    frozen = 0.25 if snapshot["phase"] == 0 else 0.75
    progress("frozen", frozen, panel)
    progress("external", 0.75, panel, rectangles["external_clip"])
    bounds = rectangles["zero"]
    record("replacement_panel_gutter", pixel((bounds[0] + bounds[2] - 8.0, bounds[1] + bounds[3] + 2.0)), panel)
    for opened, name, fraction in (("parent", "parent_progress", 0.25), ("child", "child_progress", 0.75)):
        if snapshot[opened]:
            popup = rectangles[opened]
            declaration = popup[0] + 8.0, popup[1] + (44.0 if opened == "parent" else 8.0), popup[2] - 16.0, 32.0
            geometry_matches = geometry_matches and all(abs(actual - wanted) <= 0.025
                for actual, wanted in zip(rectangles[name], declaration))
            obscured = rectangles["child"] if opened == "parent" and snapshot["child"] else None
            progress(name, fraction, (26, 34, 45), obscured=obscured)
        else:
            geometry_matches = geometry_matches and rectangles[name][2:] == (0.0, 0.0) \
                and rectangles[opened][2:] == (0.0, 0.0)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    model_matches = model_matches and snapshot["declared_frozen_bits"] == value_bits(frozen) \
        and snapshot["post_declaration_bits"] == value_bits(1.0 - frozen)
    return {
        "extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "geometry_matches": geometry_matches, "passed": model_matches and geometry_matches
            and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot))
            and all(probe["passed"] for probe in probes),
    }
