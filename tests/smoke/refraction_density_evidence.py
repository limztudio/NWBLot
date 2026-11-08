"""Validate refraction density fixture and framebuffer evidence."""

import re

from renderer_workload_log_policy import DIMENSIONS
from window_capture_smoke import SmokeFailure


DENSITY_MARKER = re.compile(r"^RefractionDensity: extent (\d+)x(\d+) requested_percent (\d+) admitted_pixels (\d+) total_pixels (\d+) active_tiles (\d+) total_tiles (\d+) glass_instances (\d+) capture_mode (visible|occluded)$", re.MULTILINE)
BOUNDS_MARKER = re.compile(r"^RefractionDensity: pixel_bounds (-?\d+) (-?\d+) (-?\d+) (-?\d+)$", re.MULTILINE)
TRACE_MARKER = re.compile(r"^RefractionDensity: tracing requested (hardware|screen-space) ray_query_supported ([01]) refraction_enabled 1$", re.MULTILINE)
CLAMP_MARKER = re.compile(r"Frame (?:Win32|X11) client size clamped from (\d+)x(\d+) to (\d+)x(\d+) for (?:work area|display) \d+x\d+")


class RefractionDensityEvidence:
    @staticmethod
    def validate_fixture(text, cell):
        text = text.replace("\r\n", "\n")
        markers, bounds, traces = DENSITY_MARKER.findall(text), BOUNDS_MARKER.findall(text), TRACE_MARKER.findall(text)
        if len(markers) != 1 or len(bounds) != 1 or len(traces) != 1:
            raise SmokeFailure("density fixture requires one unambiguous extent/coverage, bounds, and tracing marker")
        width, height, percent, pixels, total, tiles, total_tiles, instances = map(int, markers[0][:8])
        x0, y0, x1, y1 = map(int, bounds[0])
        mode = markers[0][8]
        if (width, height, percent) != (cell.width, cell.height, cell.density) or total != width * height or instances != 1:
            raise SmokeFailure("runtime density, framebuffer extent, total pixels, or retained glass instance differs from the frozen cell")
        expected_route = "hardware" if cell.route == "hardware" else "screen-space"
        if traces[0][0] != expected_route or cell.route == "hardware" and traces[0][1] != "1":
            raise SmokeFailure("density tracing settings or required hardware capability differ from the cell")
        full_tiles = ((width + 7) // 8) * ((height + 7) // 8)
        if total_tiles != full_tiles:
            raise SmokeFailure("density total tile count differs from the 8x8 framebuffer partition")
        if cell.density == 0:
            if pixels != 0 or tiles != 0 or mode != "occluded" or (x0, y0, x1, y1) != (0, 0, 0, 0):
                raise SmokeFailure("zero-density fixture must retain occluded glass with zero admitted pixels and tiles")
        else:
            left, top, right, bottom = max(0, x0), max(0, y0), min(width, x1), min(height, y1)
            if mode != "visible" or not (0 <= left < right <= width and 0 <= top < bottom <= height):
                raise SmokeFailure("visible density bounds are invalid or outside the framebuffer")
            expected_pixels = (right - left) * (bottom - top)
            expected_tiles = ((right + 7) // 8 - left // 8) * ((bottom + 7) // 8 - top // 8)
            if pixels != expected_pixels or tiles != expected_tiles:
                raise SmokeFailure("admitted pixel/tile counts disagree with analytical bounds")
            if x0 + x1 != width or y0 + y1 != height:
                raise SmokeFailure("density rectangle must remain centered")
            rounding = 2 / width + 2 / height
            if abs(pixels / total - percent / 100) > rounding or abs((x1 - x0) / width - (y1 - y0) / height) > rounding:
                raise SmokeFailure("admitted density differs beyond even-pixel rectangle rounding")
            if percent == 100 and (pixels != total or tiles != full_tiles):
                raise SmokeFailure("full density did not admit every framebuffer pixel and tile")
        return {"requested_percent": percent, "admitted_pixels": pixels, "total_pixels": total, "active_tiles": tiles, "total_tiles": total_tiles,
            "pixel_bounds": [x0, y0, x1, y1], "capture_mode": mode, "glass_instances": instances, "coverage_source": "analytical pixel-center bounds; not GPU readback"}

    @staticmethod
    def validate_extent_history(text, cell):
        target = (cell.width, cell.height)
        dimensions = [(int(match[1]), int(match[2])) for match in DIMENSIONS.finditer(text)]
        if not dimensions or dimensions[-1] != target:
            raise SmokeFailure("density renderer never confirmed its final target extent")
        clamps = list(CLAMP_MARKER.finditer(text))
        if len(clamps) > 1:
            raise SmokeFailure("multiple initial extent clamps are ambiguous")
        clamped = None
        if clamps:
            requested = tuple(map(int, clamps[0].groups()[:2]))
            clamped = tuple(map(int, clamps[0].groups()[2:]))
            if requested != target or clamped == target or min(clamped) <= 0 or clamped[0] > target[0] or clamped[1] > target[1]:
                raise SmokeFailure("initial clamp marker does not match the requested framebuffer")
            final_resize = f"GraphicsRuntime: Back buffer resized to {cell.width}x{cell.height}"
            resize_offset = text.find(final_resize, clamps[0].end())
            ready_offsets = [match.start() for match in DIMENSIONS.finditer(text) if (int(match[1]), int(match[2])) == target]
            if resize_offset < 0 or not any(offset > resize_offset for offset in ready_offsets):
                raise SmokeFailure("initial clamp lacks a confirmed final framebuffer resize")
        allowed = {target} | ({clamped} if clamped else set())
        if any(extent not in allowed for extent in dimensions):
            raise SmokeFailure("unexpected framebuffer extent is not explained by the explicit initial clamp")
        if clamped and target in dimensions and any(extent != target for extent in dimensions[dimensions.index(target):]):
            raise SmokeFailure("renderer extent changed after confirming the requested target")
        return {"target_extent": list(target), "clamped_initial_extent": list(clamped) if clamped else None}

