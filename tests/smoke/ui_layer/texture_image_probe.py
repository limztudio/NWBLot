"""Decode owned texture snapshots and qualify independent bindings, UVs and primitive overlays."""
from __future__ import annotations

import math
import re

from interaction_smoke import linear_rgb_bytes
from radio_group_probe import authored_texel
from slider_probe import compose, encoded, sampled_region
from window_capture_smoke import SmokeFailure


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiTextureImageSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiTextureImageSmoke: state sequence=(\d+) values=((?:\d+,){13}\d+)")
SOURCES = re.compile(r"UiTextureImageSmoke: sources sequence=(\d+) generations=(\d+),(\d+),(\d+) same_identity=(\d+)")
GEOMETRY = re.compile(rf"UiTextureImageSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiTextureImageSmoke: skin=(default|alternate)")
FIELDS = (
    "phase", "focus_code", "before_clicks", "after_clicks", "parent", "child", "popup_count", "image_targets",
    "declared_source", "post_handle_empty", "replacement_count", "replacement_alternate",
    "eviction_remaining", "eviction_completed",
)
RECTANGLES = (
    "before", "after", "default_tile", "alternate_tile", "tinted", "zero_alpha", "frozen", "external", "external_clip",
    "default_atlas", "alternate_atlas", "skin_fill", "parent_image", "child_image", "parent", "child",
)
DEFAULT = ((24, 29, 37), (63, 77, 93))
ALTERNATE = ((39, 63, 34), (116, 150, 79))
SEPARATOR = ((135, 156, 73), (135, 156, 73))
FILL = ((53, 131, 192), (97, 176, 233))
MARK = (235, 245, 255)
WHITE = (1.0, 1.0, 1.0, 1.0)
SENTINEL = linear_rgb_bytes((0.07, 0.13, 0.2))
POPUP = (26, 34, 45)


def snapshot_from_logs(text):
    states, displays, skins = list(STATE.finditer(text)), list(DISPLAY.finditer(text)), list(SKIN.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence, values = int(state[1]), tuple(map(int, state[2].split(",")))
    sources = [match for match in SOURCES.finditer(text) if int(match[1]) == sequence]
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in GEOMETRY.finditer(text)
        if int(match[1]) == sequence}
    if not sources or any(name not in rectangles for name in RECTANGLES):
        return None
    generations = tuple(map(int, sources[-1].groups()[1:4]))
    display = tuple(map(float, displays[-1].groups()))
    if not all(math.isfinite(value) for value in (*display, *(v for r in rectangles.values() for v in r))):
        raise SmokeFailure("texture image fixture reported nonfinite display or geometry")
    if any(value <= 0.0 for value in display) or any(r[2] < 0.0 or r[3] < 0.0 for r in rectangles.values()):
        raise SmokeFailure("texture image fixture reported invalid display or placement extents")
    if any(value > 0xFFFFFF for value in (*values, sequence)):
        raise SmokeFailure("texture image fixture exceeds its two-part displayed marker schema")
    if not all(generations) or generations[0] == generations[1]:
        raise SmokeFailure("independent concrete sources must have distinct nonzero generations")
    return {
        "sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(FIELDS, values)),
        "generations": generations, "same_identity": int(sources[-1][5]),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles,
    }


def center(rectangle):
    return rectangle[0] + rectangle[2] / 2.0, rectangle[1] + rectangle[3] / 2.0


def sampled_texture(kind, rectangle, pixel, scale, fill, border, crop=(0.0, 0.0, 24.0, 24.0)):
    """Bilinear SampleLevel(0) of a raw tile UV rectangle, including partial-tile cropping."""
    x, y, width, height = rectangle
    sx = ((pixel[0] + 0.5) / scale[0] - x) / width * crop[2] + crop[0] - 0.5
    sy = ((pixel[1] + 0.5) / scale[1] - y) / height * crop[3] + crop[1] - 0.5
    x0, y0 = math.floor(sx), math.floor(sy)
    tx, ty = sx - x0, sy - y0
    channels, alpha = [0.0, 0.0, 0.0], 0.0
    for dx, dy, weight in ((0, 0, (1.0 - tx) * (1.0 - ty)), (1, 0, tx * (1.0 - ty)),
            (0, 1, (1.0 - tx) * ty), (1, 1, tx * ty)):
        rgb, coverage = authored_texel(kind, x0 + dx, y0 + dy, fill, border)
        alpha += coverage * weight
        for channel in range(3):
            channels[channel] += rgb[channel] * weight
    return tuple(channels), alpha


def observe_texture_image(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale, rectangles = snapshot["scale"], snapshot["rectangles"]
    probes = []
    geometry_matches = snapshot["phase"] in (0, 1) and snapshot["image_targets"] == 0

    def pixel(point):
        return round(point[0] * scale[0]), round(point[1] * scale[1])

    def inside(rectangle, position):
        x, y = (position[0] + 0.5) / scale[0], (position[1] + 0.5) / scale[1]
        return rectangle[0] <= x < rectangle[0] + rectangle[2] and rectangle[1] <= y < rectangle[1] + rectangle[3]

    def record(name, position, color, tolerance=9):
        x, y = position
        if not 0 <= x < width or not 0 <= y < height:
            raise SmokeFailure(f"texture image probe '{name}' falls outside {width}x{height}: ({x},{y})")
        observed = rows[y][x]
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": list(position), "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance})

    for index, value in enumerate((*snapshot["values"], snapshot["sequence"])):
        for part in range(2):
            marker = index * 2 + part
            record(f"model_{marker}", pixel((16.0 + 12.0 * marker, snapshot["logical_extent"][1] - 14.0)),
                encoded(value >> (12 * part)), 5)
    logical_width = snapshot["logical_extent"][0]
    main_width = max(300.0, logical_width * 0.5 - 40.0)
    right = 24.0 + main_width + 32.0
    other_width = max(220.0, logical_width - right - 24.0)
    declarations = {
        "before": (32.0, 56.0, main_width - 16.0, 28.0),
        "after": (32.0, 92.0, main_width - 16.0, 28.0),
        "default_tile": (32.0, 168.0, 96.0, 64.0),
        "alternate_tile": (140.0, 168.0, 96.0, 64.0),
        "tinted": (248.0, 168.0, 96.0, 64.0),
        "zero_alpha": (32.0, 240.0, 96.0, 32.0),
        "frozen": (right + 8.0, 168.0, other_width - 16.0, 64.0),
        "external": (right + 8.0, 240.0, other_width - 16.0, 32.0),
        "external_clip": (right + 8.0, 240.0, (other_width - 16.0) * 0.5, 32.0),
        "default_atlas": (32.0, 280.0, 256.0, 128.0),
        "alternate_atlas": (right + 8.0, 280.0, 256.0, 128.0),
        "skin_fill": (right + 8.0, 416.0, other_width - 16.0, 24.0),
    }
    geometry_matches = geometry_matches and all(all(abs(actual - wanted) <= 0.025
        for actual, wanted in zip(rectangles[name], declaration)) for name, declaration in declarations.items())
    record("selected_skin_panel", pixel((44.0, 128.0)), ALTERNATE[0] if skin == "alternate" else DEFAULT[0])
    text_left, text_top = pixel((32.0, 32.0))
    text_right, text_bottom = pixel((32.0 + min(main_width - 16.0, 280.0), 48.0))
    bright_text_pixels = sum(min(rows[y][x]) >= 170
        for y in range(max(0, text_top), min(height, text_bottom))
        for x in range(max(0, text_left), min(width, text_right)))
    minimum_text_pixels = max(8, round(8.0 * scale[0] * scale[1]))
    probes.append({"name": "font_ink_coexists_with_skin_and_concrete_images", "bright_pixels": bright_text_pixels,
        "minimum": minimum_text_pixels, "passed": bright_text_pixels >= minimum_text_pixels})

    def obscured(position, parent=True):
        return (parent and snapshot["parent"] and inside(rectangles["parent"], position)) \
            or (snapshot["child"] and inside(rectangles["child"], position))

    def image(name, kind, palette, tint=WHITE, backdrop=SENTINEL, crop=(0.0, 0.0, 24.0, 24.0), constraint=None):
        bounds = rectangles[name]
        bx, by, bw, bh = bounds
        points = (
            ("center", center(bounds)), ("left", (bx + 3.0, by + bh / 2.0)),
            ("corner", (bx + 2.0, by + 2.0)), ("off_center", (bx + bw * 0.6, by + bh * 0.35)),
        )
        for label, point in points:
            position = pixel(point)
            if not inside(bounds, position) or (name != "child_image" and obscured(position, name != "parent_image")):
                continue
            if constraint is not None and not inside(constraint, position):
                color = SENTINEL
            else:
                source, alpha = sampled_texture(kind, bounds, position, scale, *palette, crop=crop)
                color = compose(source, alpha, tint, backdrop)
            record(f"{name}_{label}", position, color)
        if constraint is not None:
            cx, cy, cw, ch = constraint
            record(f"{name}_outside_clip", pixel((cx + cw + 2.0, cy + ch / 2.0)), SENTINEL)

    image("default_tile", "panel", DEFAULT)
    image("alternate_tile", "panel", ALTERNATE)
    image("tinted", "dot", (MARK, MARK), (0.4, 0.7, 0.9, 0.5))
    image("zero_alpha", "panel", DEFAULT, (1.0, 1.0, 1.0, 0.0))
    phase = snapshot["phase"]
    replacement_palette = SEPARATOR if snapshot["replacement_alternate"] else DEFAULT
    image("frozen", "panel", ALTERNATE if phase else replacement_palette,
        (0.5, 0.75, 1.0, 0.5) if phase else WHITE)
    image("external", "panel", DEFAULT, crop=(12.0, 0.0, 12.0, 24.0), constraint=rectangles["external_clip"])

    atlas_samples = {
        "default_atlas": ((16.0, 16.0, DEFAULT[0]), (112.0, 16.0, (45, 60, 80)),
            (144.0, 112.0, FILL[0]), (16.0, 144.0, MARK), (1.0, 1.0, SENTINEL)),
        "alternate_atlas": ((208.0, 112.0, ALTERNATE[0]), (16.0, 16.0, SEPARATOR[0]),
            (80.0, 16.0, FILL[0]), (240.0, 144.0, MARK), (1.0, 1.0, SENTINEL)),
    }
    for name, samples in atlas_samples.items():
        bx, by, bw, bh = rectangles[name]
        for index, (tx, ty, color) in enumerate(samples):
            position = pixel((bx + tx / 256.0 * bw, by + ty / 256.0 * bh))
            if not obscured(position):
                record(f"{name}_authored_{index}", position, color)
    bx, by, bw, bh = rectangles["skin_fill"]
    for label, point in (("center", center(rectangles["skin_fill"])), ("edge", (bx + 2.0, by + bh / 2.0))):
        position = pixel(point)
        if not obscured(position):
            source, alpha = sampled_region(rectangles["skin_fill"], position, scale, *FILL)
            record(f"skin_fill_{label}", position, compose(source, alpha, WHITE, SENTINEL))
    for opened, name, palette, kind, tint in (
            ("parent", "parent_image", ALTERNATE, "panel", WHITE),
            ("child", "child_image", (MARK, MARK), "dot", (0.5, 1.0, 0.5, 0.75))):
        if snapshot[opened]:
            popup = rectangles[opened]
            declaration = popup[0] + 8.0, popup[1] + 44.0, popup[2] - 16.0, 40.0
            geometry_matches = geometry_matches and all(abs(actual - wanted) <= 0.025
                for actual, wanted in zip(rectangles[name], declaration))
            image(name, kind, palette, tint, POPUP)
            position = pixel((popup[0] + 12.0, popup[1] + 12.0))
            if opened == "child" or not obscured(position, False):
                record(f"{opened}_manual_chrome_above_deferred_controls", position, POPUP)
        else:
            geometry_matches = geometry_matches and rectangles[name][2:] == (0.0, 0.0) \
                and rectangles[opened][2:] == (0.0, 0.0)
    generations = snapshot["generations"]
    source_matches = snapshot["same_identity"] == 1 and (generations[2] == generations[0]
        if snapshot["replacement_count"] == 0 else generations[2] not in generations[:2])
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    model_matches = model_matches and snapshot["declared_source"] == (2 if phase else 1) \
        and snapshot["post_handle_empty"] == 1
    return {
        "extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "geometry_matches": geometry_matches, "source_matches": source_matches,
        "passed": model_matches and geometry_matches and source_matches
            and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot))
            and all(probe["passed"] for probe in probes),
    }
