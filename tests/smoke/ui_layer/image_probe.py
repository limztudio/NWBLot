from __future__ import annotations

from probe_common import FrameProbeRecorder, center, compile_smoke_patterns, snapshot_from_patterns
from probe_reference import compose, linear_rgb_bytes, sampled_region, sampled_tile

LIT_VALUES = "values"
LIT_SEQUENCE = "sequence"
LIT_RECTANGLES = "rectangles"
LIT_PHASE = "phase"
LIT_EXPECTED = "expected"
LIT_PASSED = "passed"
LIT_LOGICAL_EXTENT = "logical_extent"
LIT_NATURAL_SPRITE = "natural_sprite"
LIT_NATURAL_SLICE = "natural_slice"
LIT_FIXED_SPRITE = "fixed_sprite"
LIT_FIXED_SLICE = "fixed_slice"
LIT_STRETCH = "stretch"
LIT_TINTED = "tinted"
LIT_TRANSPARENT = "transparent"
LIT_FROZEN = "frozen"
LIT_EXTERNAL = "external"
LIT_EXTERNAL_CLIP = "external_clip"
LIT_DOT = "dot"
LIT_SLICE = "slice"
LIT_PARENT = "parent"
LIT_CHILD = "child"


FIELDS = ("phase", "focus_code", "before_clicks", "after_clicks", "parent", "child", "popup_count", "image_targets", "declared_region", "post_declaration_region",)
RECTANGLES = ("before", "after", "natural_sprite", "natural_slice", "fixed_sprite", "fixed_slice", "stretch", "tinted", "transparent", "frozen", "external", "external_clip", "parent_image", "child_image", "parent", "child",)
DISPLAY, STATE, GEOMETRY, SKIN = compile_smoke_patterns("UiImageSmoke", len(FIELDS))
FILL = ((53, 131, 192), (97, 176, 233))
MARK = (235, 245, 255)
WHITE = (1.0, 1.0, 1.0, 1.0)
SENTINEL = linear_rgb_bytes((0.07, 0.13, 0.2))


def snapshot_from_logs(text):
    return snapshot_from_patterns(text, STATE, DISPLAY, SKIN, GEOMETRY, FIELDS, RECTANGLES, fixture="image", non_negative_extents=True, max_value=0xFFFFFF)


def observe_image(frame, snapshot, expected, *, extent=None, skin="default", extra=None):
    width, height, _ = frame
    scale, rectangles = snapshot["scale"], snapshot[LIT_RECTANGLES]
    recorder = FrameProbeRecorder(frame, scale, "image")
    probes = recorder.probes
    geometry_matches = snapshot[LIT_PHASE] in (0, 1) and snapshot["image_targets"] == 0
    pixel = recorder.pixel
    inside = recorder.inside
    record = lambda name, position, color, tolerance=8: recorder.record(name, position, color, tolerance, radius=0)

    recorder.probe_markers((*snapshot[LIT_VALUES], snapshot[LIT_SEQUENCE]), 16.0, 12.0, snapshot[LIT_LOGICAL_EXTENT][1] - 14.0, tolerance=5, radius=0, flat_names=True)
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    logical_width = snapshot[LIT_LOGICAL_EXTENT][0]
    main_width = max(300.0, logical_width * 0.5 - 40.0)
    right = 24.0 + main_width + 32.0
    other_width = max(220.0, logical_width - right - 24.0)
    declarations = { "before": (32.0, 56.0, main_width - 16.0, 28.0), "after": (32.0, 372.0, main_width - 16.0, 28.0), LIT_NATURAL_SPRITE: (32.0, 92.0, 24.0, 24.0), LIT_NATURAL_SLICE: (32.0, 124.0, 24.0, 24.0), LIT_FIXED_SPRITE: (32.0, 156.0, 96.0, 40.0),
        LIT_FIXED_SLICE: (32.0, 204.0, 96.0, 40.0), LIT_STRETCH: (32.0, 252.0, main_width - 16.0, 32.0), LIT_TINTED: (32.0, 292.0, 96.0, 40.0), LIT_TRANSPARENT: (32.0, 340.0, 96.0, 24.0), LIT_FROZEN: (right + 8.0, 56.0, 96.0, 40.0),
        LIT_EXTERNAL: (right + 8.0, 184.0, other_width - 16.0, 40.0), LIT_EXTERNAL_CLIP: (right + 8.0, 184.0, (other_width - 16.0) * 0.5, 40.0), }
    geometry_matches = geometry_matches and all(all(abs(actual - wanted) <= 0.025 for actual, wanted in zip(rectangles[name], declaration)) for name, declaration in declarations.items())

    def image(name, kind, tint, backdrop, constraint=None, obscured=None):
        nonlocal geometry_matches
        bounds = rectangles[name]
        bx, by, bw, bh = bounds
        geometry_matches = geometry_matches and bw > 0.0 and bh > 0.0 and bx >= 0.0 and by >= 0.0 \
            and bx + bw <= snapshot[LIT_LOGICAL_EXTENT][0] + 0.025 and by + bh <= snapshot[LIT_LOGICAL_EXTENT][1] + 0.025

        def color_at(position):
            if constraint is not None and not inside(constraint, position):
                return SENTINEL
            if kind == LIT_DOT:
                source, alpha = sampled_tile(LIT_DOT, bounds, position, scale, MARK, MARK)
            else:
                source, alpha = sampled_region(bounds, position, scale, *FILL)
            return compose(source, alpha, tint, backdrop)

        def sample(label, point):
            position = pixel(point)
            if inside(bounds, position) and (obscured is None or not inside(obscured, position)):
                record(f"{name}_{label}", position, color_at(position))

        sample("middle", center(bounds))
        if kind == LIT_DOT:
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
            for side, point in (("left", (cx - 2.0, cy + ch / 2.0)), ("right", (cx + cw + 2.0, cy + ch / 2.0))):
                position = pixel(point)
                if inside(bounds, position):
                    record(f"{name}_outside_clip_{side}", position, SENTINEL)

    for name, kind in ((LIT_NATURAL_SPRITE, LIT_DOT), (LIT_NATURAL_SLICE, LIT_SLICE), (LIT_FIXED_SPRITE, LIT_DOT), (LIT_FIXED_SLICE, LIT_SLICE), (LIT_STRETCH, LIT_SLICE)):
        image(name, kind, WHITE, panel)
    image(LIT_TINTED, LIT_DOT, (0.4, 0.7, 0.9, 0.5), panel)
    image(LIT_TRANSPARENT, LIT_DOT, (1.0, 1.0, 1.0, 0.0), panel)
    phase = snapshot[LIT_PHASE]
    image(LIT_FROZEN, LIT_SLICE if phase == 0 else LIT_DOT, WHITE if phase == 0 else (0.5, 0.75, 1.0, 0.5), panel)
    image(LIT_EXTERNAL, LIT_SLICE, WHITE, panel, constraint=rectangles[LIT_EXTERNAL_CLIP])
    natural = rectangles[LIT_NATURAL_SPRITE]
    record("replacement_panel_gutter", pixel((natural[0] + natural[2] + 8.0, natural[1] + natural[3] / 2.0)), panel)
    for opened, name, kind, tint in ((LIT_PARENT, "parent_image", LIT_SLICE, WHITE), (LIT_CHILD, "child_image", LIT_DOT, (0.5, 1.0, 0.5, 0.75))):
        if snapshot[opened]:
            popup = rectangles[opened]
            declaration = popup[0] + 8.0, popup[1] + (44.0 if opened == LIT_PARENT else 8.0), popup[2] - 16.0, 40.0
            geometry_matches = geometry_matches and all(abs(actual - wanted) <= 0.025 for actual, wanted in zip(rectangles[name], declaration))
            obscured = rectangles[LIT_CHILD] if opened == LIT_PARENT and snapshot[LIT_CHILD] else None
            image(name, kind, tint, (26, 34, 45), obscured=obscured)
        else:
            geometry_matches = geometry_matches and rectangles[name][2:] == (0.0, 0.0) \
                and rectangles[opened][2:] == (0.0, 0.0)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    model_matches = model_matches and snapshot["declared_region"] == (1 if phase == 0 else 2) \
        and snapshot["post_declaration_region"] == (2 if phase == 0 else 1)
    return { "extent": [width, height], "snapshot": snapshot, LIT_EXPECTED: dict(expected), "probes": probes, "geometry_matches": geometry_matches, LIT_PASSED: model_matches and geometry_matches
            and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot)) and all(probe[LIT_PASSED] for probe in probes), }
