"""Shared accepted-frame metadata and logical rectangle operations for UI probes."""
from __future__ import annotations

import math

from window_capture_smoke import SmokeFailure


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def snapshot_from_patterns(text, state_pattern, display_pattern, skin_pattern, geometry_pattern, fields, required_rectangles, *, fixture):
    states, displays, skins = list(state_pattern.finditer(text)), list(display_pattern.finditer(text)), list(skin_pattern.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence = int(state[1])
    values = tuple(map(int, state[2].split(",")))
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in geometry_pattern.finditer(text) if int(match[1]) == sequence}
    if any(name not in rectangles for name in required_rectangles):
        return None
    display = tuple(map(float, displays[-1].groups()))
    geometry_values = (value for rectangle in rectangles.values() for value in rectangle)
    if not all(math.isfinite(value) for value in (*display, *geometry_values)) or not all(value > 0.0 for value in display):
        raise SmokeFailure(f"{fixture} fixture reported invalid geometry or display metrics")
    return {"sequence": sequence, "skin": skins[-1][1], "values": values, **dict(zip(fields, values)), "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles}
