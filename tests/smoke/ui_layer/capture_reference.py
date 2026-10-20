"""Final SDR framebuffer probes for the authored standalone UI smoke scene."""

from dataclasses import dataclass
import math
from probe_reference import linear_rgb_bytes

LIT_NAME = "name"
LIT_RECTANGLE = "rectangle"
LIT_INK_PIXELS = "ink_pixels"
LIT_PASSED = "passed"
LIT_EXPECTED_RGB = "expected_rgb"
LIT_OBSERVED_RGB = "observed_rgb"
LIT_MAXIMUM_CHANNEL_ERROR = "maximum_channel_error"
LIT_TOLERANCE = "tolerance"
LIT_PROBES = "probes"
LIT_TEXT_CHECKS = "text_checks"


@dataclass(frozen=True)
class Probe:
    name: str
    x: float
    y: float
    expected: tuple[int, int, int]
    tolerance: int = 4


# These probes observe output behavior rather than vertices or command counts.
# Atlas colors are authored sRGB; numeric solids and tints are linear, so the expected final bytes require exactly one output conversion.
PROBES = (Probe("cleared upper margin", 0.02, 0.03, (0, 0, 0), 2), Probe("cleared lower margin", 0.90, 0.90, (0, 0, 0), 2), Probe("opaque blue", 0.08, 0.18, (0, 0, 255)), Probe("half red over blue", 0.22, 0.18, linear_rgb_bytes((0.5, 0.0, 0.5))),
    Probe("half red over transparent black", 0.42, 0.18, linear_rgb_bytes((0.5, 0.0, 0.0))), Probe("linear quarter gray", 0.62, 0.18, linear_rgb_bytes((0.25, 0.25, 0.25))), Probe("half tinted white skin", 0.86, 0.18, linear_rgb_bytes((0.05, 0.4, 0.1)), 6),
    Probe("outside nested clips left", 0.08, 0.50, linear_rgb_bytes((0.0, 0.2, 0.3))), Probe("outer clip only", 0.12, 0.50, (0, 255, 0)), Probe("nested clip intersection", 0.20, 0.50, (255, 0, 0)),
    Probe("outside nested clips right", 0.28, 0.50, linear_rgb_bytes((0.0, 0.2, 0.3))), Probe("panel nine-slice center", 0.49, 0.52, (24, 29, 37), 8), Probe("normal button nine-slice center", 0.725, 0.47, (45, 60, 80), 8),
    Probe("hover button nine-slice center", 0.89, 0.47, (60, 84, 112), 8), Probe("sprite transparent corner", 0.687, 0.687, linear_rgb_bytes((0.0, 0.1, 0.2)), 6), Probe("sprite opaque arrow", 0.72, 0.72, (235, 245, 255), 8),
    Probe("glyph zero coverage", 0.665, 0.865, (0, 0, 0), 2), Probe("glyph half coverage half tint opacity", 0.745, 0.865, linear_rgb_bytes((128 / 255 * 0.5, 128 / 255 * 0.1, 128 / 255 * 0.3))),
    Probe("glyph full coverage half tint opacity", 0.825, 0.865, linear_rgb_bytes((0.5, 0.1, 0.3))), Probe("SDF R exterior", 0.665, 0.96, (0, 0, 0), 2), Probe("SDF G zero distance half tint opacity", 0.745, 0.96, linear_rgb_bytes((0.05, 0.25, 0.10))),
    Probe("SDF B interior", 0.825, 0.96, linear_rgb_bytes((0.10, 0.50, 0.20))), Probe("SDF A exterior", 0.886, 0.96, (0, 0, 0), 2), Probe("SDF A zero distance half tint opacity", 0.905, 0.96, linear_rgb_bytes((0.05, 0.25, 0.10))),
    Probe("SDF A interior", 0.924, 0.96, linear_rgb_bytes((0.10, 0.50, 0.20))),)


def analyze_text_region(rows, name, left, top, right, bottom, minimum_ink=80):
    pixels = [pixel for row in rows[top:bottom] for pixel in row[left:right]]
    ink = sum(max(pixel) > 20 for pixel in pixels)
    edge_levels = {pixel[0] for pixel in pixels if 20 < pixel[0] < 235}
    return { LIT_NAME: name, LIT_RECTANGLE: [left, top, right, bottom], LIT_INK_PIXELS: ink, "distinct_edge_levels": len(edge_levels), LIT_PASSED: ink >= minimum_ink and len(edge_levels) >= 8, }


def analyze_latin_descenders(rows, width, height):
    # The 22-pixel Latin label starts at (0.04 * width, 0.80 * height).
    # These ranges isolate the four final glyphs in "office ffi e\u0301 gqyp" without depending on the viewport extent.
    label_left = math.floor(width * 0.04)
    descender_row = math.floor(height * 0.80) + 29
    glyph_ranges = (("g", 107, 121), ("q", 123, 135), ("y", 134, 144), ("p", 145, 160))
    checks = []
    for glyph, left_offset, right_offset in glyph_ranges:
        left = label_left + left_offset
        right = label_left + right_offset
        ink = sum(max(pixel) > 20 for pixel in rows[descender_row][left:right])
        checks.append({ LIT_NAME: f"Latin {glyph} descender reaches lower antialiasing row", LIT_RECTANGLE: [left, descender_row, right, descender_row + 1], LIT_INK_PIXELS: ink, LIT_PASSED: ink >= 2, })
    return checks


def analyze_frame(frame):
    width, height, rows = frame
    if width < 320 or height < 240:
        raise ValueError(f"UI capture is too small for the defined probes: {width}x{height}")
    results = []
    for probe in PROBES:
        center_x = min(width - 3, max(2, math.floor(probe.x * width)))
        center_y = min(height - 3, max(2, math.floor(probe.y * height)))
        pixels = [rows[y][x] for y in range(center_y - 2, center_y + 3) for x in range(center_x - 2, center_x + 3)]
        observed = tuple(sorted(pixel[channel] for pixel in pixels)[len(pixels) // 2] for channel in range(3))
        error = max(abs(actual - expected) for actual, expected in zip(observed, probe.expected))
        results.append({ LIT_NAME: probe.name, "coordinate": [center_x, center_y], LIT_EXPECTED_RGB: list(probe.expected), LIT_OBSERVED_RGB: list(observed), LIT_MAXIMUM_CHANNEL_ERROR: error, LIT_TOLERANCE: probe.tolerance, LIT_PASSED: error <= probe.tolerance, })
    text_regions = (analyze_text_region(rows, "Latin ligature and combining-mark label", math.floor(width * 0.04), math.floor(height * 0.80), math.floor(width * 0.32), math.floor(height * 0.87)), analyze_text_region(rows, "Korean fallback label",
            math.floor(width * 0.04), math.floor(height * 0.875), math.floor(width * 0.32), math.floor(height * 0.97)), analyze_text_region(rows, "Clipped label",
            math.floor(width * 0.34), math.floor(height * 0.82), math.floor(width * 0.55), math.floor(height * 0.90)),)
    outside_pixels = [pixel for row in rows[math.floor(height * 0.82):math.floor(height * 0.90)] for pixel in row[math.ceil(width * 0.55) + 1:math.floor(width * 0.63)]]
    clipped_outside = { LIT_NAME: "Clipped label has no ink beyond clip", LIT_INK_PIXELS: sum(max(pixel) > 2 for pixel in outside_pixels), LIT_PASSED: all(max(pixel) <= 2 for pixel in outside_pixels), }
    text_checks = [*text_regions, *analyze_latin_descenders(rows, width, height), clipped_outside]
    return {"width": width, "height": height, LIT_PASSED: all(probe[LIT_PASSED] for probe in results) and all(check[LIT_PASSED] for check in text_checks), LIT_PROBES: results, LIT_TEXT_CHECKS: text_checks}


def failure_description(report):
    pixel_failures = [f"{probe[LIT_NAME]}: expected {probe[LIT_EXPECTED_RGB]}, observed {probe[LIT_OBSERVED_RGB]} " f"(error {probe[LIT_MAXIMUM_CHANNEL_ERROR]}, tolerance {probe[LIT_TOLERANCE]})" for probe in report[LIT_PROBES] if not probe[LIT_PASSED]]
    text_failures = [f"{check[LIT_NAME]}: {check}" for check in report[LIT_TEXT_CHECKS] if not check[LIT_PASSED]]
    return "; ".join([*pixel_failures, *text_failures])
