"""Decode owned texture snapshots and qualify independent bindings, UVs and deferred Builder images."""
from __future__ import annotations

import math
import re

from window_capture_smoke import SmokeFailure
from probe_reference import compose, encoded_marker, linear_rgb_bytes, sampled_region, sampled_tile

LIT_BUILDER_SOURCE = "builder_source"
LIT_BUILDER_HANDLE_EMPTY = "builder_handle_empty"
LIT_SEQUENCE = "sequence"
LIT_SKIN = "skin"
LIT_VALUES = "values"
LIT_GENERATIONS = "generations"
LIT_SAME_IDENTITY = "same_identity"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_SCALE = "scale"
LIT_RECTANGLES = "rectangles"
LIT_PHASE = "phase"
LIT_NAME = "name"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_DEFAULT_TILE = "default_tile"
LIT_ALTERNATE_TILE = "alternate_tile"
LIT_TINTED = "tinted"
LIT_ZERO_ALPHA = "zero_alpha"
LIT_FROZEN = "frozen"
LIT_EXTERNAL = "external"
LIT_EXTERNAL_CLIP = "external_clip"
LIT_DEFAULT_ATLAS = "default_atlas"
LIT_ALTERNATE_ATLAS = "alternate_atlas"
LIT_SKIN_FILL = "skin_fill"
LIT_BUILDER_IMAGE = "builder_image"
LIT_ALTERNATE = "alternate"
LIT_PARENT = "parent"
LIT_CHILD = "child"
LIT_CENTER = "center"
LIT_CHILD_IMAGE = "child_image"
LIT_PARENT_IMAGE = "parent_image"
LIT_PANEL = "panel"
LIT_DOT = "dot"
LIT_REPLACEMENT_ALTERNATE = "replacement_alternate"


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiTextureImageSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiTextureImageSmoke: state sequence=(\d+) values=((?:\d+,){15}\d+)")
SOURCES = re.compile(r"UiTextureImageSmoke: sources sequence=(\d+) generations=(\d+),(\d+),(\d+) same_identity=(\d+)")
GEOMETRY = re.compile(rf"UiTextureImageSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiTextureImageSmoke: skin=(default|alternate)")
FIELDS = ("phase", "focus_code", "before_clicks", "after_clicks", "parent", "child", "popup_count", "image_targets", "declared_source", "post_handle_empty", "replacement_count", "replacement_alternate",
    "eviction_remaining", "eviction_completed", LIT_BUILDER_SOURCE, LIT_BUILDER_HANDLE_EMPTY,)
RECTANGLES = ("before", "after", "default_tile", "alternate_tile", "tinted", "zero_alpha", "frozen", "external", "external_clip", "default_atlas", "alternate_atlas", "skin_fill", "parent_image", "child_image", "parent", "child", "builder_image",)
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
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in GEOMETRY.finditer(text) if int(match[1]) == sequence}
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
    return { LIT_SEQUENCE: sequence, LIT_SKIN: skins[-1][1], LIT_VALUES: values, **dict(zip(FIELDS, values)), LIT_GENERATIONS: generations, LIT_SAME_IDENTITY: int(sources[-1][5]), LIT_LOGICAL_EXTENT: display[:2], LIT_SCALE: display[2:], LIT_RECTANGLES: rectangles, }


def center(rectangle):
    return rectangle[0] + rectangle[2] / 2.0, rectangle[1] + rectangle[3] / 2.0


def observe_texture_image(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, rows = frame
    scale, rectangles = snapshot[LIT_SCALE], snapshot[LIT_RECTANGLES]
    probes = []
    geometry_matches = snapshot[LIT_PHASE] in (0, 1) and snapshot["image_targets"] == 0

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
        probes.append({LIT_NAME: name, "position": list(position), LIT_EXPECTED: list(color), "observed": list(observed), "error": error, LIT_PASSED: error <= tolerance})

    for index, value in enumerate((*snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE])):
        for part in range(2):
            marker = index * 2 + part
            record(f"model_{marker}", pixel((16.0 + 12.0 * marker, snapshot[LIT_LOGICAL_EXTENT][1] - 14.0)), encoded_marker(value >> (12 * part)), 5)
    logical_width = snapshot[LIT_LOGICAL_EXTENT][0]
    main_width = max(300.0, logical_width * 0.5 - 40.0)
    right = 24.0 + main_width + 32.0
    other_width = max(220.0, logical_width - right - 24.0)
    declarations = { "before": (32.0, 56.0, main_width - 16.0, 28.0), "after": (32.0, 92.0, main_width - 16.0, 28.0), LIT_DEFAULT_TILE: (32.0, 168.0, 96.0, 64.0), LIT_ALTERNATE_TILE: (140.0, 168.0, 96.0, 64.0), LIT_TINTED: (248.0, 168.0, 96.0, 64.0),
        LIT_ZERO_ALPHA: (32.0, 240.0, 96.0, 32.0), LIT_FROZEN: (right + 8.0, 168.0, other_width - 16.0, 64.0), LIT_EXTERNAL: (right + 8.0, 240.0, other_width - 16.0, 32.0), LIT_EXTERNAL_CLIP: (right + 8.0, 240.0, (other_width - 16.0) * 0.5, 32.0),
        LIT_DEFAULT_ATLAS: (32.0, 280.0, 256.0, 128.0), LIT_ALTERNATE_ATLAS: (right + 8.0, 280.0, 256.0, 128.0), LIT_SKIN_FILL: (right + 8.0, 416.0, other_width - 16.0, 24.0), LIT_BUILDER_IMAGE: (right + 8.0, 32.0, other_width - 16.0, 80.0), }
    geometry_matches = geometry_matches and all(all(abs(actual - wanted) <= 0.025 for actual, wanted in zip(rectangles[name], declaration)) for name, declaration in declarations.items())
    record("selected_skin_panel", pixel((44.0, 128.0)), ALTERNATE[0] if skin == LIT_ALTERNATE else DEFAULT[0])
    text_left, text_top = pixel((32.0, 32.0))
    text_right, text_bottom = pixel((32.0 + min(main_width - 16.0, 280.0), 48.0))
    bright_text_pixels = sum(min(rows[y][x]) >= 170 for y in range(max(0, text_top), min(height, text_bottom)) for x in range(max(0, text_left), min(width, text_right)))
    minimum_text_pixels = max(8, round(8.0 * scale[0] * scale[1]))
    probes.append({LIT_NAME: "font_ink_coexists_with_skin_and_concrete_images", "bright_pixels": bright_text_pixels, "minimum": minimum_text_pixels, LIT_PASSED: bright_text_pixels >= minimum_text_pixels})

    def obscured(position, parent=True):
        return (parent and snapshot[LIT_PARENT] and inside(rectangles[LIT_PARENT], position)) \
            or (snapshot[LIT_CHILD] and inside(rectangles[LIT_CHILD], position))

    def image(name, kind, palette, tint=WHITE, backdrop=SENTINEL, crop=(0.0, 0.0, 24.0, 24.0), constraint=None):
        bounds = rectangles[name]
        bx, by, bw, bh = bounds
        points = ((LIT_CENTER, center(bounds)), ("left", (bx + 3.0, by + bh / 2.0)), ("corner", (bx + 2.0, by + 2.0)), ("off_center", (bx + bw * 0.6, by + bh * 0.35)),)
        for label, point in points:
            position = pixel(point)
            if not inside(bounds, position) or (name != LIT_CHILD_IMAGE and obscured(position, name != LIT_PARENT_IMAGE)):
                continue
            if constraint is not None and not inside(constraint, position):
                color = SENTINEL
            else:
                source, alpha = sampled_tile(kind, bounds, position, scale, *palette, crop=crop)
                color = compose(source, alpha, tint, backdrop)
            record(f"{name}_{label}", position, color)
        if constraint is not None:
            cx, cy, cw, ch = constraint
            record(f"{name}_outside_clip", pixel((cx + cw + 2.0, cy + ch / 2.0)), SENTINEL)

    image(LIT_DEFAULT_TILE, LIT_PANEL, DEFAULT)
    image(LIT_ALTERNATE_TILE, LIT_PANEL, ALTERNATE)
    image(LIT_TINTED, LIT_DOT, (MARK, MARK), (0.4, 0.7, 0.9, 0.5))
    image(LIT_ZERO_ALPHA, LIT_PANEL, DEFAULT, (1.0, 1.0, 1.0, 0.0))
    phase = snapshot[LIT_PHASE]
    replacement_palette = SEPARATOR if snapshot[LIT_REPLACEMENT_ALTERNATE] else DEFAULT
    image(LIT_FROZEN, LIT_PANEL, ALTERNATE if phase else replacement_palette, (0.5, 0.75, 1.0, 0.5) if phase else WHITE)
    image(LIT_EXTERNAL, LIT_PANEL, DEFAULT, crop=(12.0, 0.0, 12.0, 24.0), constraint=rectangles[LIT_EXTERNAL_CLIP])

    def atlas(name, alternate, tint=WHITE, backdrop=SENTINEL):
        bounds = rectangles[name]
        bx, by, bw, bh = bounds
        separator = SEPARATOR if alternate else ((65, 80, 99), (65, 80, 99))
        samples = [(16.0, 16.0, LIT_PANEL, (4.0, 4.0), separator if alternate else DEFAULT), (208.0, 112.0, LIT_PANEL, (196.0, 100.0), ALTERNATE if alternate else separator),]
        samples += ([(80.0, 16.0, LIT_PANEL, (68.0, 4.0), FILL), (240.0, 144.0, LIT_DOT, (228.0, 132.0), (MARK, MARK))] if alternate else [(112.0, 16.0, LIT_PANEL, (100.0, 4.0), ((45, 60, 80), (79, 101, 126))), (144.0, 112.0, LIT_PANEL, (132.0, 100.0), FILL),
                (16.0, 144.0, LIT_DOT, (4.0, 132.0), (MARK, MARK))])
        samples.append((1.0, 1.0, LIT_PANEL, (4.0, 4.0), DEFAULT))
        for index, (tx, ty, kind, origin, palette) in enumerate(samples):
            position = pixel((bx + tx / 256.0 * bw, by + ty / 256.0 * bh))
            if not inside(bounds, position) or (name != LIT_CHILD_IMAGE and obscured(position, name != LIT_PARENT_IMAGE)):
                continue
            source, alpha = sampled_tile(kind, bounds, position, scale, *palette, crop=(-origin[0], -origin[1], 256.0, 256.0))
            record(f"{name}_full_atlas_{index}", position, compose(source, alpha, tint, backdrop))

    atlas(LIT_BUILDER_IMAGE, bool(phase or snapshot[LIT_REPLACEMENT_ALTERNATE]), (0.5, 0.75, 1.0, 0.5) if phase else WHITE, ALTERNATE[0] if skin == LIT_ALTERNATE else DEFAULT[0])

    atlas_samples = { LIT_DEFAULT_ATLAS: ((16.0, 16.0, DEFAULT[0]), (112.0, 16.0, (45, 60, 80)), (144.0, 112.0, FILL[0]), (16.0, 144.0, MARK), (1.0, 1.0, SENTINEL)), LIT_ALTERNATE_ATLAS: ((208.0, 112.0, ALTERNATE[0]), (16.0, 16.0, SEPARATOR[0]),
            (80.0, 16.0, FILL[0]), (240.0, 144.0, MARK), (1.0, 1.0, SENTINEL)), }
    for name, samples in atlas_samples.items():
        bx, by, bw, bh = rectangles[name]
        for index, (tx, ty, color) in enumerate(samples):
            position = pixel((bx + tx / 256.0 * bw, by + ty / 256.0 * bh))
            if not obscured(position):
                record(f"{name}_authored_{index}", position, color)
    bx, by, bw, bh = rectangles[LIT_SKIN_FILL]
    for label, point in ((LIT_CENTER, center(rectangles[LIT_SKIN_FILL])), ("edge", (bx + 2.0, by + bh / 2.0))):
        position = pixel(point)
        if not obscured(position):
            source, alpha = sampled_region(rectangles[LIT_SKIN_FILL], position, scale, *FILL)
            record(f"skin_fill_{label}", position, compose(source, alpha, WHITE, SENTINEL))
    for opened, name, alternate, tint in ((LIT_PARENT, LIT_PARENT_IMAGE, True, WHITE), (LIT_CHILD, LIT_CHILD_IMAGE, False, (0.5, 1.0, 0.5, 0.75))):
        if snapshot[opened]:
            popup = rectangles[opened]
            top = 44.0 if opened == LIT_PARENT else 8.0
            declaration = popup[0] + 8.0, popup[1] + top, popup[2] - 16.0, 64.0
            geometry_matches = geometry_matches and all(abs(actual - wanted) <= 0.025 for actual, wanted in zip(rectangles[name], declaration))
            atlas(name, alternate, tint, POPUP)
            position = pixel((popup[0] + 12.0, popup[1] + 120.0))
            if opened == LIT_CHILD or not obscured(position, False):
                record(f"{opened}_actual_builder_chrome_over_late_base", position, POPUP)
        else:
            geometry_matches = geometry_matches and rectangles[name][2:] == (0.0, 0.0) \
                and rectangles[opened][2:] == (0.0, 0.0)
    generations = snapshot[LIT_GENERATIONS]
    source_matches = snapshot[LIT_SAME_IDENTITY] == 1 and (generations[2] == generations[0] if snapshot["replacement_count"] == 0 else generations[2] not in generations[:2])
    model_matches = snapshot[LIT_SKIN] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    model_matches = model_matches and snapshot["declared_source"] == (2 if phase else 1) \
        and snapshot["post_handle_empty"] == 1 and snapshot[LIT_BUILDER_SOURCE] == (2 if phase else 1) \
        and snapshot[LIT_BUILDER_HANDLE_EMPTY] == 1
    return { "extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "geometry_matches": geometry_matches, "source_matches": source_matches, LIT_PASSED: model_matches and geometry_matches and source_matches
            and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot)) and all(probe[LIT_PASSED] for probe in probes), }
