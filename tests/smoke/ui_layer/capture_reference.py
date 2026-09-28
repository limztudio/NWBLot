"""Final SDR framebuffer probes for the authored standalone UI smoke scene."""

from dataclasses import dataclass
import math


@dataclass(frozen=True)
class Probe:
    name: str
    x: float
    y: float
    expected: tuple[int, int, int]
    tolerance: int = 4


def linear_to_srgb_bytes(rgb):
    def encode(channel):
        encoded = channel * 12.92 if channel <= 0.0031308 else 1.055 * channel ** (1.0 / 2.4) - 0.055
        return round(encoded * 255.0)

    return tuple(encode(channel) for channel in rgb)


# These probes observe output behavior rather than vertices or command counts. Atlas colors are authored sRGB;
# numeric solids and tints are linear, so the expected final bytes require exactly one output conversion.
PROBES = (
    Probe("cleared upper margin", 0.02, 0.03, (0, 0, 0), 2),
    Probe("cleared lower margin", 0.90, 0.90, (0, 0, 0), 2),
    Probe("opaque blue", 0.08, 0.18, (0, 0, 255)),
    Probe("half red over blue", 0.22, 0.18, linear_to_srgb_bytes((0.5, 0.0, 0.5))),
    Probe("half red over transparent black", 0.42, 0.18, linear_to_srgb_bytes((0.5, 0.0, 0.0))),
    Probe("linear quarter gray", 0.62, 0.18, linear_to_srgb_bytes((0.25, 0.25, 0.25))),
    Probe("half tinted white skin", 0.86, 0.18, linear_to_srgb_bytes((0.05, 0.4, 0.1)), 6),
    Probe("outside nested clips left", 0.08, 0.50, linear_to_srgb_bytes((0.0, 0.2, 0.3))),
    Probe("outer clip only", 0.12, 0.50, (0, 255, 0)),
    Probe("nested clip intersection", 0.20, 0.50, (255, 0, 0)),
    Probe("outside nested clips right", 0.28, 0.50, linear_to_srgb_bytes((0.0, 0.2, 0.3))),
    Probe("panel nine-slice center", 0.49, 0.52, (24, 29, 37), 8),
    Probe("normal button nine-slice center", 0.725, 0.47, (45, 60, 80), 8),
    Probe("hover button nine-slice center", 0.89, 0.47, (60, 84, 112), 8),
    Probe("sprite transparent corner", 0.687, 0.687, linear_to_srgb_bytes((0.0, 0.1, 0.2)), 6),
    Probe("sprite opaque arrow", 0.72, 0.72, (235, 245, 255), 8),
    Probe("glyph zero coverage", 0.665, 0.865, (0, 0, 0), 2),
    Probe("glyph half coverage half tint opacity", 0.745, 0.865,
        linear_to_srgb_bytes((128 / 255 * 0.5, 128 / 255 * 0.1, 128 / 255 * 0.3))),
    Probe("glyph full coverage half tint opacity", 0.825, 0.865, linear_to_srgb_bytes((0.5, 0.1, 0.3))),
    Probe("SDF R exterior", 0.665, 0.96, (0, 0, 0), 2),
    Probe("SDF G zero distance half tint opacity", 0.745, 0.96, linear_to_srgb_bytes((0.05, 0.25, 0.10))),
    Probe("SDF B interior", 0.825, 0.96, linear_to_srgb_bytes((0.10, 0.50, 0.20))),
    Probe("SDF A exterior", 0.886, 0.96, (0, 0, 0), 2),
    Probe("SDF A zero distance half tint opacity", 0.905, 0.96, linear_to_srgb_bytes((0.05, 0.25, 0.10))),
    Probe("SDF A interior", 0.924, 0.96, linear_to_srgb_bytes((0.10, 0.50, 0.20))),
)


def analyze_text_region(rows, name, left, top, right, bottom, minimum_ink=80):
    pixels = [pixel for row in rows[top:bottom] for pixel in row[left:right]]
    ink = sum(max(pixel) > 20 for pixel in pixels)
    edge_levels = {pixel[0] for pixel in pixels if 20 < pixel[0] < 235}
    return {
        "name": name,
        "rectangle": [left, top, right, bottom],
        "ink_pixels": ink,
        "distinct_edge_levels": len(edge_levels),
        "passed": ink >= minimum_ink and len(edge_levels) >= 8,
    }


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
        results.append({
            "name": probe.name,
            "coordinate": [center_x, center_y],
            "expected_rgb": list(probe.expected),
            "observed_rgb": list(observed),
            "maximum_channel_error": error,
            "tolerance": probe.tolerance,
            "passed": error <= probe.tolerance,
        })
    text_regions = (
        analyze_text_region(rows, "Latin ligature and combining-mark label",
            math.floor(width * 0.04), math.floor(height * 0.80), math.floor(width * 0.32), math.floor(height * 0.87)),
        analyze_text_region(rows, "Korean fallback label",
            math.floor(width * 0.04), math.floor(height * 0.875), math.floor(width * 0.32), math.floor(height * 0.97)),
        analyze_text_region(rows, "Clipped label",
            math.floor(width * 0.34), math.floor(height * 0.82), math.floor(width * 0.55), math.floor(height * 0.90)),
    )
    outside_pixels = [pixel for row in rows[math.floor(height * 0.82):math.floor(height * 0.90)]
        for pixel in row[math.ceil(width * 0.55) + 1:math.floor(width * 0.63)]]
    clipped_outside = {
        "name": "Clipped label has no ink beyond clip",
        "ink_pixels": sum(max(pixel) > 2 for pixel in outside_pixels),
        "passed": all(max(pixel) <= 2 for pixel in outside_pixels),
    }
    text_checks = [*text_regions, clipped_outside]
    return {"width": width, "height": height,
        "passed": all(probe["passed"] for probe in results) and all(check["passed"] for check in text_checks),
        "probes": results, "text_checks": text_checks}


def failure_description(report):
    pixel_failures = [
        f"{probe['name']}: expected {probe['expected_rgb']}, observed {probe['observed_rgb']} "
        f"(error {probe['maximum_channel_error']}, tolerance {probe['tolerance']})"
        for probe in report["probes"] if not probe["passed"]
    ]
    text_failures = [f"{check['name']}: {check}" for check in report["text_checks"] if not check["passed"]]
    return "; ".join([*pixel_failures, *text_failures])
