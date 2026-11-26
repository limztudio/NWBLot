from __future__ import annotations

import math
from pathlib import Path
import re
import sys

SMOKE_DIRECTORY = str(Path(__file__).resolve().parent.parent)
if SMOKE_DIRECTORY not in sys.path:
    sys.path.insert(0, SMOKE_DIRECTORY)

from window_capture_smoke import SmokeFailure
from probe_reference import encoded_marker, linear_channels, linear_rgb_bytes


FNV32_OFFSET_BASIS = 2166136261
FNV32_PRIME = 16777619
UINT32_MASK = 0xFFFFFFFF
NUMBER = r"[0-9.eE+-]+"
MARKER_TOLERANCE = 5
LIT_NAME = "name"
LIT_POSITION = "position"
LIT_EXPECTED = "expected"
LIT_OBSERVED = "observed"
LIT_ERROR = "error"
LIT_PASSED = "passed"


def center(rectangle):
    x, y, width, height = rectangle
    return x + width / 2.0, y + height / 2.0


def text_hash(text):
    value = FNV32_OFFSET_BASIS
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * FNV32_PRIME) & UINT32_MASK
    return value


def compile_smoke_patterns(prefix, field_count, *, bit_count=0):
    display = re.compile(rf"{prefix}: display logical=({NUMBER})x({NUMBER}) scale=({NUMBER})x({NUMBER})")
    state_body = rf"{prefix}: state sequence=(\d+) values=((?:\d+,){{{field_count - 1}}}\d+)"
    if bit_count > 0:
        state_body += rf" bits=((?:\d+,){{{bit_count - 1}}}\d+)"
    state = re.compile(state_body)
    geometry = re.compile(rf"{prefix}: geometry sequence=(\d+) (\w+)=({NUMBER}),({NUMBER}),({NUMBER}),({NUMBER})")
    skin = re.compile(rf"{prefix}: skin=(default|alternate)")
    return display, state, geometry, skin


def ink_coverage(observed, background, foreground):
    actual, backdrop = linear_channels(observed), linear_channels(background)
    direction = tuple(target - base for target, base in zip(foreground, backdrop))
    denominator = sum(component * component for component in direction)
    coverage = sum((value - base) * component for value, base, component in zip(actual, backdrop, direction)) / denominator
    coverage = max(0.0, min(1.0, coverage))
    expected = linear_rgb_bytes(tuple(base + coverage * component for base, component in zip(backdrop, direction)))
    residual = max(abs(value - target) for value, target in zip(observed, expected))
    return coverage, residual, expected


def snapshot_from_patterns(text, state_pattern, display_pattern, skin_pattern, geometry_pattern, fields, required_rectangles, *, fixture, bit_fields=(),
        non_negative_extents=False, max_value=None, max_bit=None, bit_validator=None):
    states, displays, skins = list(state_pattern.finditer(text)), list(display_pattern.finditer(text)), list(skin_pattern.finditer(text))
    if not states or not displays or not skins:
        return None
    state = states[-1]
    sequence = int(state[1])
    values = tuple(map(int, state[2].split(",")))
    bits = tuple(map(int, state[3].split(","))) if bit_fields else ()
    rectangles = {match[2]: tuple(map(float, match.groups()[2:])) for match in geometry_pattern.finditer(text) if int(match[1]) == sequence}
    if any(name not in rectangles for name in required_rectangles):
        return None
    display = tuple(map(float, displays[-1].groups()))
    geometry_values = (value for rectangle in rectangles.values() for value in rectangle)
    if not all(math.isfinite(value) for value in (*display, *geometry_values)) or not all(value > 0.0 for value in display):
        raise SmokeFailure(f"{fixture} fixture reported invalid geometry or display metrics")
    if non_negative_extents and any(rectangle[2] < 0.0 or rectangle[3] < 0.0 for rectangle in rectangles.values()):
        raise SmokeFailure(f"{fixture} fixture reported a negative placement extent")
    if max_value is not None and any(value > max_value for value in (*values, sequence)):
        raise SmokeFailure(f"{fixture} fixture exceeds its displayed marker schema")
    if max_bit is not None and any(value > max_bit for value in bits):
        raise SmokeFailure(f"{fixture} fixture exceeds its displayed marker schema")
    if bit_validator is not None and not all(bit_validator(value) for value in bits):
        raise SmokeFailure(f"{fixture} fixture reported a nonfinite typed value")
    return {"sequence": sequence, "skin": skins[-1][1], "values": values, **({"bits": bits} if bit_fields else {}), **dict(zip(fields, values)), **dict(zip(bit_fields, bits)),
        "logical_extent": display[:2], "scale": display[2:], "rectangles": rectangles}


class FrameProbeRecorder:
    def __init__(self, frame, scale, fixture):
        self.width, self.height, self.rows = frame
        self.scale = scale
        self.fixture = fixture
        self.probes = []

    def pixel(self, point):
        return round(point[0] * self.scale[0]), round(point[1] * self.scale[1])

    def inside(self, rectangle, position):
        x, y = (position[0] + 0.5) / self.scale[0], (position[1] + 0.5) / self.scale[1]
        return rectangle[0] <= x < rectangle[0] + rectangle[2] and rectangle[1] <= y < rectangle[1] + rectangle[3]

    def sample_rgb(self, x, y, *, radius=1):
        if not radius <= x < self.width - radius or not radius <= y < self.height - radius:
            raise SmokeFailure(f"{self.fixture} probe falls outside {self.width}x{self.height}: ({x},{y})")
        if radius == 0:
            return self.rows[y][x]
        pixels = [self.rows[py][px] for py in range(y - radius, y + radius + 1) for px in range(x - radius, x + radius + 1)]
        return tuple(sorted(pixel[channel] for pixel in pixels)[len(pixels) // 2] for channel in range(3))

    def record(self, name, pixel, color, tolerance=MARKER_TOLERANCE, *, radius=0):
        x, y = pixel
        if not radius <= x < self.width - radius or not radius <= y < self.height - radius:
            raise SmokeFailure(f"{self.fixture} probe '{name}' falls outside {self.width}x{self.height}: ({x},{y})")
        observed = self.sample_rgb(x, y, radius=radius)
        error = max(abs(actual - reference) for actual, reference in zip(observed, color))
        result = {LIT_NAME: name, LIT_POSITION: [x, y], LIT_EXPECTED: list(color), LIT_OBSERVED: list(observed), LIT_ERROR: error, LIT_PASSED: error <= tolerance}
        self.probes.append(result)
        return result

    def probe(self, name, point, color, tolerance=MARKER_TOLERANCE, *, radius=1):
        return self.record(name, self.pixel(point), color, tolerance, radius=radius)

    def probe_markers(self, values, origin_x, stride_x, y, *, parts=2, tolerance=MARKER_TOLERANCE, radius=1, flat_names=False, dim=1.0):
        part_counts = (parts,) * len(values) if isinstance(parts, int) else tuple(parts)
        marker = 0
        for index, (value, count) in enumerate(zip(values, part_counts)):
            for part in range(count):
                name = f"model_{marker}" if flat_names else (f"model_{index}" if count == 1 else f"model_{index}_{part}")
                self.probe(name, (origin_x + marker * stride_x, y), encoded_marker(value >> (part * 12), dim), tolerance, radius=radius)
                marker += 1
        return marker
