"""Decode frozen radio snapshots and compare model, atlas and placement pixels."""
from __future__ import annotations

import math
import re

from interaction_smoke import linear_rgb_bytes
from window_capture_smoke import SmokeFailure


NUMBER = r"[0-9.eE+-]+"
DISPLAY = re.compile(rf"UiRadioGroupSmoke: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
STATE = re.compile(r"UiRadioGroupSmoke: state sequence=(\d+) values=((?:\d+,){21}\d+)")
GEOMETRY = re.compile(rf"UiRadioGroupSmoke: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
SKIN = re.compile(r"UiRadioGroupSmoke: skin=(default|alternate)")
FIELDS = ("main_selected", "main_cursor", "focus_code", "changes", "activations", "before_clicks", "after_clicks",
    "enabled", "source_revision", "source_generation", "reversed", "removed", "parent", "popup_selected",
    "popup_cursor", "popup_changes", "popup_activations", "popup_count", "focus_scope", "label_reads",
    "disabled_selected", "choice_count")
KEYS = (10, 20, 30, 40, 50)
RECTANGLES = ("before", "group", "after", "open", "disabled_group", "parent", "popup_group", "popup_close",
    "disabled_row30", "disabled_indicator30", "disabled_mark30") + tuple(
        f"{prefix}{part}{key}" for prefix in ("", "popup_") for key in KEYS for part in ("row", "indicator", "mark"))
MARKER_TOLERANCE = 5
SKIN_TOLERANCE = 8
NORMAL = ((20, 27, 36), (81, 103, 129))
CHECKED = ((40, 102, 164), (110, 186, 245))
MARK = (235, 245, 255)


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
    geometry = tuple(value for rectangle in rectangles.values() for value in rectangle)
    if not all(math.isfinite(value) for value in (*display, *geometry)) or not all(value > 0.0 for value in display):
        raise SmokeFailure("radio fixture reported invalid geometry or display metrics")
    if any(rectangle[2] < 0.0 or rectangle[3] < 0.0 for rectangle in rectangles.values()):
        raise SmokeFailure("radio fixture reported a negative placement extent")
    if any(value > 0xFFFFFF for value in (*values, sequence)):
        raise SmokeFailure("radio fixture exceeds its two-part 24-bit displayed marker schema")
    return {"sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(FIELDS, values)),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles}


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def encoded(value):
    return linear_rgb_bytes(tuple(((value >> shift) & 15) / 15.0 for shift in (0, 4, 8)))


def linear_channels(color):
    return tuple(channel / 255.0 / 12.92 if channel <= 10 else
        ((channel / 255.0 + 0.055) / 1.055) ** 2.4 for channel in color)


def compose(source, alpha, tint, backdrop):
    background = linear_channels(backdrop)
    opacity = alpha * tint[3]
    return linear_rgb_bytes(tuple(channel * tint[index] * opacity + background[index] * (1.0 - opacity)
        for index, channel in enumerate(source)))


def authored_texel(kind, x, y, fill, border):
    """Match the authored four-by-four straight-alpha tile, before UASTC compression."""
    if not 0 <= x < 24 or not 0 <= y < 24:
        return (0.0, 0.0, 0.0), 0.0
    samples = []
    for sy in range(4):
        for sx in range(4):
            px, py = x + (sx + 0.5) / 4.0, y + (sy + 0.5) / 4.0
            if kind == "dot":
                if math.hypot(px - 12.0, py - 12.0) <= 5.0:
                    samples.append(MARK)
            else:
                dx, dy = abs(px - 12.0) - 7.0, abs(py - 12.0) - 7.0
                distance = math.hypot(max(dx, 0.0), max(dy, 0.0)) + min(max(dx, dy), 0.0) - 5.0
                if distance <= 0.0:
                    samples.append(border if distance >= -1.5 else fill)
    if not samples:
        return (0.0, 0.0, 0.0), 0.0
    rgb = tuple(round(sum(sample[channel] for sample in samples) / len(samples)) for channel in range(3))
    return linear_channels(rgb), round(len(samples) * 255.0 / 16.0) / 255.0


def sampled_tile(kind, rectangle, pixel, scale, fill, border):
    x, y, width, height = rectangle
    sx = ((pixel[0] + 0.5) / scale[0] - x) / width * 24.0 - 0.5
    sy = ((pixel[1] + 0.5) / scale[1] - y) / height * 24.0 - 0.5
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


def observe_radio_group(frame, snapshot, expected, *, extent=None, skin="default", extra=None, pressed=None):
    width, height, rows = frame
    scale = snapshot["scale"]
    rectangles = snapshot["rectangles"]
    probes = []

    def record(name, pixel, color, tolerance=SKIN_TOLERANCE):
        x, y = pixel
        if not 0 <= x < width or not 0 <= y < height:
            raise SmokeFailure(f"radio probe '{name}' falls outside {width}x{height}: ({x},{y})")
        observed = rows[y][x]
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        result = {"name": name, "position": [x, y], "expected": list(color), "observed": list(observed),
            "error": error, "passed": error <= tolerance}
        probes.append(result)
        return result

    def probe(name, point, color, tolerance=SKIN_TOLERANCE):
        pixel = round(point[0] * scale[0]), round(point[1] * scale[1])
        return record(name, pixel, color, tolerance)

    for index, value in enumerate((*snapshot["values"], snapshot["sequence"])):
        for part in range(2):
            probe(f"model_{index}_{part}", (16.0 + 12.0 * (index * 2 + part), snapshot["logical_extent"][1] - 14.0),
                encoded(value >> (part * 12)), MARKER_TOLERANCE)
    panel = (39, 63, 34) if skin == "alternate" else (24, 29, 37)
    gx, gy, gw, gh = rectangles["group"]
    probe("replacement_panel_gutter", (gx + gw - 8.0, gy + gh + 2.0), panel)
    geometry_matches = snapshot["label_reads"] <= 15 and snapshot["choice_count"] in (4, 5)

    def contains(outer, inner):
        ox, oy, ow, oh = outer
        ix, iy, iw, ih = inner
        return iw > 0.0 and ih > 0.0 and ix >= ox - 0.01 and iy >= oy - 0.01 \
            and ix + iw <= ox + ow + 0.01 and iy + ih <= oy + oh + 0.01

    def choice(prefix, key, group, selected, enabled, backdrop):
        nonlocal geometry_matches
        indicator, mark = rectangles[f"{prefix}indicator{key}"], rectangles[f"{prefix}mark{key}"]
        row = rectangles[f"{prefix}row{key}"]
        if row[2] <= 0.0 or row[3] <= 0.0:
            geometry_matches = geometry_matches and key == snapshot["removed"] \
                and indicator[2:] == (0.0, 0.0) and mark[2:] == (0.0, 0.0)
            return
        geometry_matches = geometry_matches and key != snapshot["removed"] \
            and contains(group, row) and contains(row, indicator) and contains(indicator, mark)
        geometry_matches = geometry_matches and row[3] >= 32.0 and abs(indicator[2] - 24.0) <= 0.01
        geometry_matches = geometry_matches and abs(indicator[3] - 24.0) <= 0.01
        geometry_matches = geometry_matches and abs(mark[2] - indicator[2] * 0.4) <= 0.01
        geometry_matches = geometry_matches and abs(mark[3] - indicator[3] * 0.4) <= 0.01
        if prefix == "disabled_" and snapshot["parent"] and contains(rectangles["parent"], indicator) \
                and contains(rectangles["parent"], mark):
            probes.append({"name": f"{prefix}indicator{key}_occluded_by_parent_popup", "occluded": True,
                "occluding_rectangle": list(rectangles["parent"]), "indicator_rectangle": list(indicator),
                "mark_rectangle": list(mark), "passed": True})
            return
        fill, border = CHECKED if key == selected else NORMAL
        tint = (0.55, 0.55, 0.55, 0.6) if not enabled or key == 30 else (
            (0.85, 0.85, 0.85, 1.0) if pressed == (prefix, key) else (1.0, 1.0, 1.0, 1.0))
        ix, iy, iw, ih = indicator
        probe(f"{prefix}indicator{key}_fill", (ix + 4.0, iy + ih / 2.0),
            compose(linear_channels(fill), 1.0, tint, backdrop))
        # At scaled DPI the first border pixel can include alpha/filtering. Compare authored samples at each pixel center.
        candidates = []
        # The whole-row focus overlay crosses the indicator's left edge; its right border remains independent.
        left = max(0, math.floor((ix + iw - 1.5) * scale[0]))
        right = min(width, math.ceil((ix + iw) * scale[0]))
        top = round((iy + ih / 2.0) * scale[1])
        for px in range(left, right) if 0 <= top < height else ():
            source, alpha = sampled_tile("panel", indicator, (px, top), scale, fill, border)
            if alpha < 0.7:
                continue
            color = compose(source, alpha, tint, backdrop)
            observed = rows[top][px]
            error = max(abs(actual - reference) for actual, reference in zip(observed, color))
            candidates.append((error, px, color))
        if candidates:
            _, px, color = min(candidates)
            record(f"{prefix}indicator{key}_border", (px, top), color)
        else:
            probes.append({"name": f"{prefix}indicator{key}_border", "passed": False})
        mx, my, mw, mh = mark
        for name, u, v in (("center", 0.5, 0.5), ("offcenter", 0.60, 0.35)):
            pixel = round((mx + mw * u) * scale[0]), round((my + mh * v) * scale[1])
            source, alpha = sampled_tile("dot", mark, pixel, scale, fill, border)
            color = compose(source, alpha, tint, compose(linear_channels(fill), 1.0, tint, backdrop)) \
                if key == selected else compose(linear_channels(fill), 1.0, tint, backdrop)
            record(f"{prefix}mark{key}_{name}", pixel, color)

    for key in KEYS:
        choice("", key, rectangles["group"], snapshot["main_selected"], bool(snapshot["enabled"]), panel)
    choice("disabled_", 30, rectangles["disabled_group"], snapshot["disabled_selected"], False, panel)
    if snapshot["parent"]:
        popup = rectangles["parent"]
        geometry_matches = geometry_matches and contains((0.0, 0.0, *snapshot["logical_extent"]), popup)
        for key in KEYS:
            choice("popup_", key, rectangles["popup_group"], snapshot["popup_selected"], True, (26, 34, 45))
    else:
        for name in ("parent", "popup_group", "popup_close"):
            geometry_matches = geometry_matches and rectangles[name][2:] == (0.0, 0.0)
        for key in KEYS:
            for part in ("row", "indicator", "mark"):
                geometry_matches = geometry_matches and rectangles[f"popup_{part}{key}"][2:] == (0.0, 0.0)
    model_matches = snapshot["skin"] == skin and all(snapshot[name] == int(value) for name, value in expected.items())
    return {"extent": [width, height], "snapshot": snapshot, "expected": dict(expected), "probes": probes,
        "geometry_matches": geometry_matches, "passed": model_matches and geometry_matches
            and (extent is None or (width, height) == extent) and (extra is None or extra(snapshot))
            and all(item["passed"] for item in probes)}
