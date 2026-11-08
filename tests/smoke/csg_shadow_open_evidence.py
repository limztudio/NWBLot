#!/usr/bin/env python3
"""Validate open-shadow runtime receipts and image evidence."""

import re

from csg_shadow_reference import BYTE_TOLERANCE, region_pixels
from csg_shadow_geometry import CsgShadowGeometry
import window_capture_smoke as capture
from window_capture_smoke import SmokeFailure

LIT_WARNING_TAG = "[WARNING]"
LIT_ENABLED = "enabled"
LIT_HARDWARE = "hardware"
LIT_SOFTWARE = "software"
LIT_DISABLED = "disabled"
LIT_TERMINAL65 = "terminal65"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_ROUTE = "route"
LIT_RGB = "rgb"
LIT_RETAINED = "retained"
LIT_CLIPPED = "clipped"

CASES = ("reference", "retained", "clipped_reference", "clipped", "boundary64", "terminal65")
ORDINARY_CASES = ("reference", "clipped_reference")
OPEN_CAPACITY_MESSAGE = "RendererSystem: CSG direct-shadow traversal exceeded open-surface capacity (flags=16); conservative occlusion retained"
COUNTS = {"reference": (5, 6), "retained": (5, 6), "clipped_reference": (4, 5), "clipped": (4, 5), "boundary64": (64, 6), "terminal65": (65, 6)}
LENGTHS = {"reference": (2., 1.2), "retained": (2., 1.2), "clipped_reference": (1., 1.6), "clipped": (1., 1.6), "boundary64": (64. / 63., 1.2), "terminal65": (None, 1.2)}
IOR = 1.5
REGIONS = (("first_receiver", -1.2, 0.), ("six_sheet_control", 1.2, 0.), ("clear_receiver", 0., 1.2))
SRGB_CHANNEL_MAX = 255.0
SRGB_LINEAR_SLOPE = 12.92
SRGB_LINEAR_CUTOFF = 0.0031308
SRGB_GAMMA_SCALE = 1.055
SRGB_GAMMA_OFFSET = 0.055
SRGB_GAMMA_EXPONENT = 2.4


class CsgOpenShadowRuntimeEvidence:
    @staticmethod
    def validate_runtime(log_text, case, route, frames, warning_policy=LIT_ENABLED):
        if case not in CASES or route not in (LIT_HARDWARE, LIT_SOFTWARE) or warning_policy not in (LIT_ENABLED, LIT_DISABLED):
            raise SmokeFailure("unknown open-shadow case, route or warning policy")
        lines = log_text.splitlines()
        warning_indices = [index for index, line in enumerate(lines) if LIT_WARNING_TAG in line]
        remaining = log_text
        if case == LIT_TERMINAL65 and warning_policy == LIT_ENABLED:
            if len(warning_indices) != 1:
                raise SmokeFailure("terminal65 requires only the exact open-capacity flags=16 diagnostic")
            start = warning_indices[0]
            if not re.fullmatch(r"\d{2}:\d{2}:\d{2}\.\d{3} \[WARNING\]:", lines[start]):
                raise SmokeFailure("terminal65 warning has an invalid severity-record header")
            end = next((index for index in range(start + 1, len(lines)) if re.fullmatch(r"\d{2}:\d{2}:\d{2}\.\d{3} \[[A-Z ]+\]:", lines[index])), len(lines))
            message = "\n".join(lines[start + 1:end]).strip("\n")
            if message != OPEN_CAPACITY_MESSAGE:
                raise SmokeFailure("terminal65 requires the complete exact open-capacity flags=16 warning message")
            # Waive only this exact severity and message pair.
            remaining = "\n".join(lines[:start] + lines[end:])
        elif warning_indices:
            raise SmokeFailure(f"{case}: valid open shadow emitted a warning")
        for marker in capture.STRICT_LOG_FAILURE_MESSAGES:
            if marker in remaining:
                raise SmokeFailure(f"{case}: rejected runtime diagnostic {marker}")
        first, second = COUNTS[case]
        required = ["FramebufferCapture: capture ready", "CsgShadowSmokeProject: shutdown", f"CsgShadowSmokeProject: open_case={case} retained_events={first},{second} receiver_z=0 sheets_z_min=-7 sheets_z_max=-5 ior=1.5 unit_transmission=0.25,0.5,0.75 coverage=1",
            f"CsgShadowSmokeProject: atlas arm=1 light=directional hardware={int(route == LIT_HARDWARE)}",
            "SoftwareShadowSmoke: requested backend=0 directional_resolution=512 point_resolution=256 budget_bytes=268435456 coverage=0 blocker_search=0 capture_cadence=0"]
        if route == LIT_SOFTWARE:
            required.append("Loader: hardware ray tracing disabled before device creation")
            if LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA in log_text:
                raise SmokeFailure("software arm used hardware transparent shadows")
        if case in ORDINARY_CASES:
            required.append(LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA if route == LIT_HARDWARE else "RendererSystem: dispatched light-space shadow maps")
            if "RendererSystem: dispatched CSG light-space shadows" in log_text:
                raise SmokeFailure("ordinary open reference admitted CSG shadow work")
        else:
            required.append(f"RendererSystem: dispatched CSG light-space shadows (hardware_compose={int(route == LIT_HARDWARE)}")
        for message in required:
            if message not in log_text:
                raise SmokeFailure(f"{case}: missing exact runtime evidence: {message}")
        sources = [int(value) for value in re.findall(r"FramebufferCapture: graphics source frame (\d+)", log_text)]
        if len(sources) != 1 or sources[0] < frames - 1:
            raise SmokeFailure(f"{case}: capture lacks one completed source after the required frame warmup")
        flags_observed = case == LIT_TERMINAL65 and warning_policy == LIT_ENABLED
        return {"graphics_source_frame": sources[0], LIT_ROUTE: route, "retained_events": [first, second], "diagnostic_flags": 16 if flags_observed else None,
            "diagnostic_flags_observed": flags_observed, "warning_policy": warning_policy, "capacity_diagnostic_available": warning_policy == LIT_ENABLED, "expected_capacity_terminal": case == LIT_TERMINAL65}


class CsgOpenShadowImageEvidence:
    @staticmethod
    def expected_open_rgb(length, events):
        if length is None:
            return (0., 0., 0.)
        interface_transmission = (1. - ((IOR - 1.) / (IOR + 1.)) ** 2) ** events
        linear = [channel ** length * interface_transmission for channel in CsgShadowGeometry.TRANSMISSION]
        return tuple(SRGB_CHANNEL_MAX * (SRGB_LINEAR_SLOPE * value if value <= SRGB_LINEAR_CUTOFF else SRGB_GAMMA_SCALE * value ** (1. / SRGB_GAMMA_EXPONENT) - SRGB_GAMMA_OFFSET) for value in linear)

    @staticmethod
    def measure_frame(frame, case):
        width, height, rows = frame
        if width < 320 or height < 240 or len(rows) != height or any(len(row) != width for row in rows):
            raise SmokeFailure("invalid open-shadow capture extent")
        measurements = {}
        for index, region in enumerate(REGIONS):
            pixels = region_pixels(region, width, height)
            rgb = tuple(sum(rows[y][x][channel] for x, y in pixels) / len(pixels) for channel in range(3))
            length = LENGTHS[case][index] if index < 2 else 0.
            events = COUNTS[case][index] if index < 2 else 0
            expected = CsgOpenShadowImageEvidence.expected_open_rgb(length, events)
            tolerance = 4. if length is None else BYTE_TOLERANCE
            error = max(abs(value - target) for value, target in zip(rgb, expected))
            if error > tolerance:
                raise SmokeFailure(f"{case}/{region[0]}: open shadow disagrees with independent pairing/Beer/Fresnel oracle; rgb={rgb}, expected={expected}")
            measurements[region[0]] = {LIT_RGB: rgb, "expected_rgb": expected, "optical_length": length, "interface_count": events, "maximum_mean_byte_error": error, "samples": len(pixels)}
        return measurements

    @staticmethod
    def compare_frames(frames):
        if set(frames) != set(CASES) or len({frame[:2] for frame in frames.values()}) != 1:
            raise SmokeFailure("open-shadow qualification requires all six matched capture cases")
        measured = {case: CsgOpenShadowImageEvidence.measure_frame(frame, case) for case, frame in frames.items()}
        for reference, csg in (("reference", LIT_RETAINED), ("clipped_reference", LIT_CLIPPED)):
            for region in REGIONS:
                difference = max(abs(a - b) for a, b in zip(measured[reference][region[0]][LIT_RGB], measured[csg][region[0]][LIT_RGB]))
                if difference > BYTE_TOLERANCE:
                    raise SmokeFailure(f"{csg}: retained open optics differ from independent ordinary reference")
        for name in ("first_receiver", "six_sheet_control"):
            change = max(abs(a - b) for a, b in zip(measured[LIT_RETAINED][name][LIT_RGB], measured[LIT_CLIPPED][name][LIT_RGB]))
            if change < 15.:
                raise SmokeFailure(f"{name}: clipping did not change the open pairing control")
        return {"oracle": "independent odd span/even pairs, clipped ordinary geometry, Beer absorption and IOR1.5 interface Fresnel through the 64/65 boundary", "ior": IOR, "unit_transmission": CsgShadowGeometry.TRANSMISSION,
            "mean_rgb_tolerance_bytes": BYTE_TOLERANCE, "regions": measured}
