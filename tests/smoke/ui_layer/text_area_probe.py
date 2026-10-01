"""Compare multiline model markers, two-axis geometry, and independent GPU pixels."""
from __future__ import annotations

import math
import re

from interaction_smoke import linear_rgb_bytes
from numeric_edit_probe import center, encoded, ink_coverage, linear_channels, text_hash
from window_capture_smoke import SmokeFailure


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiTextAreaSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiTextAreaSmoke: state sequence=(\d+) values=((?:\d+,){24}\d+)")
GEOMETRY = re.compile(rf"UiTextAreaSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
METRICS = re.compile(rf"UiTextAreaSmoke: metrics sequence=(\d+) scroll=({NUMBER}),({NUMBER}) measure=({NUMBER}),({NUMBER}) line_height=({NUMBER}) selections=(\d+) maximum=({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiTextAreaSmoke: skin=(default|alternate)")
FIELDS = ("bytes", "hash", "anchor", "caret", "focus", "enabled", "readonly", "compact", "long_document",
    "preferred_valid", "preferred_bits", "submits", "cancels", "blurs", "abandons", "outside", "clipboard_seeds",
    "clipboard_pending", "model_revision", "external_revision", "selection_generation", "coherent", "line_count",
    "can_undo", "can_redo")
RECTANGLES = ("bounds", "content", "clip", "caret", "reset", "long", "readonly", "enabled", "viewport", "clipboard", "outside",
    "x_track", "x_thumb", "y_track", "y_thumb", "corner")
MARKER_PARTS = tuple(3 if index in (1, 10, 18, 19, 20) else 2 for index in range(len(FIELDS))) + (2,)


def document_fields(text, anchor, caret):
    return {"bytes": len(text.encode("utf-8")), "hash": text_hash(text), "anchor": anchor, "caret": caret,
        "line_count": text.count("\n") + 1}


def snapshot_from_logs(text):
    states, displays, skins = list(STATE.finditer(text)), list(DISPLAY.finditer(text)), list(SKIN.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence = int(state[1])
    values = tuple(map(int, state[2].split(",")))
    display = tuple(map(float, displays[-1].groups()))
    metrics = [item for item in METRICS.finditer(text) if int(item[1]) == sequence]
    rectangles = {item[2]: tuple(map(float, item.groups()[2:])) for item in GEOMETRY.finditer(text)
        if int(item[1]) == sequence}
    if not metrics or any(name not in rectangles for name in RECTANGLES):
        return None
    metric = metrics[-1]
    scroll, measure = tuple(map(float, metric.groups()[1:3])), tuple(map(float, metric.groups()[3:5]))
    line_height, count = float(metric[6]), int(metric[7])
    maximum = tuple(map(float, metric.groups()[7:9]))
    if count > 32 or any(f"selection_{index}" not in rectangles for index in range(count)):
        return None
    coordinates = [value for rectangle in rectangles.values() for value in rectangle]
    if not all(math.isfinite(value) for value in (*display, *scroll, *measure, *maximum, line_height, *coordinates)):
        raise SmokeFailure("TextArea fixture reported nonfinite geometry")
    if any(value <= 0.0 for value in display) or line_height <= 0.0 or any(value < 0.0 for value in (*scroll, *maximum)):
        raise SmokeFailure("TextArea fixture reported invalid display, line height, or scroll")
    return {"sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(FIELDS, values)),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles,
        "scroll": scroll, "measure": measure, "maximum": maximum, "line_height": line_height, "selection_count": count}


def observe_text_area(frame, snapshot, expected, *, skin="default", extent=None, extra=None, minimum_selection_lines=0,
    allow_offscreen_caret=False):
    width, height, rows = frame
    scale_x, scale_y = snapshot["scale"]
    probes = []

    def probe(name, position, color, tolerance=5, radius=1):
        x, y = round(position[0] * scale_x), round(position[1] * scale_y)
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"TextArea probe '{name}' is outside {width}x{height}: ({x},{y})")
        pixels = [rows[py][px] for py in range(y - radius, y + radius + 1) for px in range(x - radius, x + radius + 1)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[len(pixels) // 2] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        probes.append({"name": name, "position": [x, y], "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance})

    marker = 0
    for index, (value, parts) in enumerate(zip((*snapshot["values"], snapshot["sequence"]), MARKER_PARTS)):
        for part in range(parts):
            probe(f"model_{index}_{part}", (11.0 + marker * 8.0, snapshot["logical_extent"][1] - 13.0),
                encoded(value >> (part * 12)))
            marker += 1
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    probe("panel_atlas", (28.0, 394.0), panel, 8)
    bx, by, bw, bh = snapshot["rectangles"]["bounds"]
    cx, cy, cw, ch = snapshot["rectangles"]["content"]
    lx, ly, lw, lh = snapshot["rectangles"]["clip"]
    kx, ky, kw, kh = snapshot["rectangles"]["caret"]
    expected_width, expected_height = (120.0, 72.0) if snapshot["compact"] else (380.0, 160.0)
    geometry_matches = snapshot["coherent"] == 1 and marker == sum(MARKER_PARTS)
    geometry_matches &= abs(bw - expected_width) <= 0.75 and abs(bh - expected_height) <= 0.75
    geometry_matches &= cw > 0.0 and ch > 0.0 and kw > 0.0 and kh > 0.0
    geometry_matches &= cx >= bx and cy >= by and cx + cw <= bx + bw + 0.75 and cy + ch <= by + bh + 0.75
    geometry_matches &= abs(cx - lx) <= 0.75 and abs(cy - ly) <= 0.75 and abs(cw - lw) <= 0.75 and abs(ch - lh) <= 0.75
    caret_visible = cx - 0.75 <= kx and kx + kw <= cx + cw + 0.75 and cy - 0.75 <= ky and ky + kh <= cy + ch + 0.75
    geometry_matches &= allow_offscreen_caret or caret_visible
    expected_maximum = (max(0.0, snapshot["measure"][0] + kw - cw), max(0.0, snapshot["measure"][1] - ch))
    geometry_matches &= all(abs(actual - wanted) <= 0.05 for actual, wanted in zip(snapshot["maximum"], expected_maximum))
    geometry_matches &= all(0.0 <= offset <= maximum + 0.05 for offset, maximum in zip(snapshot["scroll"], snapshot["maximum"]))
    enabled, focused = bool(snapshot["enabled"]), bool(snapshot["focus"])
    background = (29, 33, 40) if not enabled else (18, 26, 37) if focused else (15, 20, 28)
    probe("edit_atlas", (bx + bw - 4.0, by + bh - 6.0), background, 5 if focused else 8)
    probe("horizontal_clip_gutter", (bx + bw + 4.0, by + bh / 2.0), panel, 8)
    thumb_color = ((49, 100, 63) if skin == "alternate" else (45, 60, 80)) if enabled else \
        ((54, 60, 42) if skin == "alternate" else (37, 43, 52))
    bar_visibility = []
    for axis, name in enumerate(("x", "y")):
        tx, ty, tw, th = snapshot["rectangles"][name + "_track"]
        sx, sy, sw, sh = snapshot["rectangles"][name + "_thumb"]
        visible = tw > 0.0 and th > 0.0
        bar_visibility.append(visible)
        geometry_matches &= visible == (snapshot["maximum"][axis] > 0.0)
        if not visible:
            geometry_matches &= (tx, ty, tw, th, sx, sy, sw, sh) == (0.0,) * 8
            continue
        horizontal = axis == 0
        origin, length = (tx, tw) if horizontal else (ty, th)
        thumb_origin, thumb_length = (sx, sw) if horizontal else (sy, sh)
        viewport = cw if horizontal else ch
        maximum, offset = snapshot["maximum"][axis], snapshot["scroll"][axis]
        if maximum <= 0.0 or viewport <= 0.0:
            raise SmokeFailure(f"TextArea {name} scrollbar has no positive maximum or viewport")
        expected_length = min(length, max(min(20.0, length), length * viewport / (viewport + maximum)))
        expected_origin = origin + (length - expected_length) * offset / maximum
        geometry_matches &= abs(length - viewport) <= 0.05 and abs(thumb_length - expected_length) <= 0.05
        geometry_matches &= abs(thumb_origin - expected_origin) <= 0.05
        geometry_matches &= bx <= tx and by <= ty and tx + tw <= bx + bw and ty + th <= by + bh
        geometry_matches &= sx >= tx and sy >= ty and sx + sw <= tx + tw + 0.05 and sy + sh <= ty + th + 0.05
        geometry_matches &= abs((ty if horizontal else tx) - ((cy + ch) if horizontal else (cx + cw))) <= 0.05
        free_before, free_after = thumb_origin - origin, origin + length - thumb_origin - thumb_length
        free_length = max(free_before, free_after)
        if free_length >= 4.0:
            track_at = origin + free_before / 2.0 if free_before >= free_after else thumb_origin + thumb_length + free_after / 2.0
            probe(name + "_track_atlas", (track_at, ty + th / 2.0) if horizontal else (tx + tw / 2.0, track_at), panel, 8)
        probe(name + "_thumb_atlas", (sx + sw / 2.0, sy + sh / 2.0), thumb_color, 8)
    ox, oy, ow, oh = snapshot["rectangles"]["corner"]
    if all(bar_visibility):
        geometry_matches &= abs(ox - cx - cw) <= 0.05 and abs(oy - cy - ch) <= 0.05 and ow > 0.0 and oh > 0.0
        probe("corner_atlas", center((ox, oy, ow, oh)), panel, 8, radius=0)
    else:
        geometry_matches &= (ox, oy, ow, oh) == (0.0,) * 4
    foreground = (0.92, 0.94, 0.98) if enabled else (0.48, 0.50, 0.55)
    if snapshot["bytes"] and snapshot["anchor"] == snapshot["caret"]:
        caret_column = round(kx * scale_x)
        left, right = max(0, math.ceil(cx * scale_x)), min(width, math.floor((cx + min(cw, 200.0)) * scale_x))
        top, bottom = max(0, math.ceil(cy * scale_y)), min(height, math.floor((cy + ch) * scale_y))
        best = None
        for y in range(top, bottom):
            for x in range(left, right):
                if abs(x - caret_column) <= 2:
                    continue
                coverage, residual, color = ink_coverage(rows[y][x], background, foreground)
                candidate = (residual, -coverage, x, y, color, rows[y][x])
                if coverage >= 0.25 and (best is None or candidate < best):
                    best = candidate
        if best is None:
            probes.append({"name": "text_ink", "coverage": 0.0, "passed": False})
        else:
            residual, negative_coverage, x, y, color, observed = best
            probes.append({"name": "text_ink", "position": [x, y], "expected": list(color), "observed": list(observed),
                "coverage": -negative_coverage, "error": residual, "passed": residual <= 5})
    visible_selections = 0
    if focused and snapshot["anchor"] != snapshot["caret"]:
        backdrop = linear_channels(background)
        selection_color = linear_rgb_bytes(tuple(selected * 0.75 + base * 0.25
            for selected, base in zip((0.20, 0.40, 0.78), backdrop)))
        for index in range(snapshot["selection_count"]):
            sx, sy, sw, sh = snapshot["rectangles"][f"selection_{index}"]
            left, top, right, bottom = max(sx, lx), max(sy, ly), min(sx + sw, lx + lw), min(sy + sh, ly + lh)
            if right - left < 4.0 or bottom - top < 4.0:
                continue
            visible_selections += 1
            # A clipped first/last line can end inside its glyphs. Find a solid selected
            # patch inside this line's clipped rectangle, retaining the exact blend gate.
            pixel_left = max(1, math.ceil(left * scale_x) + 1)
            pixel_right = min(width - 2, math.floor(min(right, left + 72.0) * scale_x) - 1)
            pixel_top = max(1, math.ceil(top * scale_y) + 1)
            pixel_bottom = min(height - 2, math.floor(bottom * scale_y) - 1)
            best = None
            for y in range(pixel_top, pixel_bottom + 1, max(1, round(scale_y))):
                for x in range(pixel_left, pixel_right + 1, max(1, round(scale_x))):
                    pixels = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
                    observed = tuple(sorted(pixel[channel] for pixel in pixels)[4] for channel in range(3))
                    error = max(abs(actual - reference) for actual, reference in zip(observed, selection_color))
                    candidate = (error, x, y, observed)
                    if best is None or candidate < best:
                        best = candidate
                    if error <= 5:
                        break
                if best is not None and best[0] <= 5:
                    break
            if best is None:
                probes.append({"name": f"selection_line_{index}", "passed": False})
            else:
                error, x, y, observed = best
                probes.append({"name": f"selection_line_{index}", "position": [x, y],
                    "expected": list(selection_color), "observed": list(observed), "error": error, "passed": error <= 5})
    geometry_matches &= visible_selections >= minimum_selection_lines
    if focused and enabled and not snapshot["readonly"] and snapshot["anchor"] == snapshot["caret"] and caret_visible:
        geometry_matches &= abs(kw * scale_x - 1.0) <= 0.01
        column, row = round(kx * scale_x), round((ky + kh - 0.75) * scale_y)
        if not 0 <= column < width or not 0 <= row < height:
            raise SmokeFailure("TextArea caret probe is outside the captured client")
        color = linear_rgb_bytes((0.95, 0.97, 1.0))
        pixels = [rows[y][column] for y in range(max(0, row - 1), min(height, row + 2))]
        error = min(max(abs(actual - reference) for actual, reference in zip(pixel, color)) for pixel in pixels)
        probes.append({"name": "caret", "position": [column, row], "expected": list(color),
            "observed": [list(pixel) for pixel in pixels], "error": error, "tolerance": 5, "passed": error <= 5})
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    extra_matches = extra is None or bool(extra(snapshot))
    return {"extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "geometry_matches": bool(geometry_matches), "model_matches": model_matches, "extra_matches": extra_matches,
        "visible_selection_lines": visible_selections, "marker_count": marker, "caret_visible": caret_visible,
        "allow_offscreen_caret": allow_offscreen_caret,
        "passed": model_matches and geometry_matches and extra_matches and (extent is None or (width, height) == extent)
            and all(item["passed"] for item in probes)}
