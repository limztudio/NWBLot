#!/usr/bin/env python3
"""Independent receiver-world samples and Beer-law oracle for the CSG shadow atlas."""

import math

from window_capture_smoke import SmokeFailure

# Shared literals (no inline hardcodes below this block).
LIT_REFERENCE = "reference"
LIT_CUT = "cut"
LIT_UNCUT = "uncut"
LIT_MOVED = "moved"
LIT_CAMERA_SHIFT = "camera_shift"
LIT_CLEAR = "clear"
LIT_OPAQUE_HOLE = "opaque_hole"
LIT_GLASS_DEPTH = "glass_depth"
LIT_GLASS_HOLE = "glass_hole"
LIT_OVERLAP = "overlap"
LIT_POINT = "point"
LIT_DIRECTIONAL = "directional"
LIT_CSG_SHADOW_CAPTURE_DIMENSIONS_ARE_INVA = "CSG shadow capture dimensions are invalid"
LIT_RGB = "rgb"
LIT_EXPECTED_RGB = "expected_rgb"
LIT_MAXIMUM_MEAN_BYTE_ERROR = "maximum_mean_byte_error"
LIT_SAMPLE_COUNT = "sample_count"
LIT_CSG_SHADOW_CAPTURES_HAVE_DIFFERENT_EXT = "CSG shadow captures have different extents"
LIT_LIGHT = "light"
LIT_EXTENT = "extent"
LIT_MEAN_RGB_TOLERANCE_BYTES = "mean_rgb_tolerance_bytes"
LIT_ORACLE = "oracle"
LIT_REGIONS = "regions"
LIT_PLANE_LEFT = "plane_left"
LIT_ELLIPSOID_CENTER = "ellipsoid_center"
LIT_CAPSULE_BODY = "capsule_body"
LIT_AXIAL_CAPSULE_CENTER = "axial_capsule_center"
LIT_SPHERE_UNION_CENTER = "sphere_union_center"


ARMS = (LIT_REFERENCE, LIT_CUT, LIT_UNCUT, LIT_MOVED, LIT_CAMERA_SHIFT)
ANALYTIC_ARMS = (LIT_CUT, LIT_UNCUT)
TRANSMISSION = (0.25, 0.5, 0.75)
BYTE_TOLERANCE = 12.0
# All regions are inside constant-thickness areas, away from raster and denoiser boundaries.
# Values are (name, receiver x, receiver y, cut optical length, uncut optical length); None means opaque.
REGIONS = (
    (LIT_CLEAR, 0.0, 0.0, 0.0, 0.0),
    (LIT_OPAQUE_HOLE, -1.7, 1.0, 0.0, None),
    ("opaque_old_hole", -2.3, 1.0, None, None),
    ("opaque_rim", -2.5, 1.0, None, None),
    ("opaque_cap", 0.0, 1.0, None, None),
    (LIT_GLASS_DEPTH, 2.0, 1.0, 1.0, 2.0),
    (LIT_GLASS_HOLE, -1.7, -1.0, 0.0, 2.0),
    ("glass_old_hole", -2.3, -1.0, 2.0, 2.0),
    ("glass_rim", -2.5, -1.0, 2.0, 2.0),
    (LIT_OVERLAP, 0.0, -1.0, 2.0, 4.0),
    ("glass_control", 2.0, -1.0, 2.0, 2.0),
)


def expected_rgb(length, world_x, world_y, light):
    if length is None:
        return (0.0, 0.0, 0.0)
    if light == LIT_POINT:
        length *= math.sqrt(12.0 ** 2 + world_x ** 2 + world_y ** 2) / 12.0
    linear = [channel ** length for channel in TRANSMISSION]
    return tuple(255.0 * (12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055)
        for value in linear)


def ray_box(origin, direction, lower, upper):
    near, far = 0.0, math.inf
    for axis in range(3):
        if direction[axis] == 0.0:
            if origin[axis] < lower[axis] or origin[axis] > upper[axis]:
                return None
            continue
        first = (lower[axis] - origin[axis]) / direction[axis]
        second = (upper[axis] - origin[axis]) / direction[axis]
        near, far = max(near, min(first, second)), min(far, max(first, second))
    return (near, far) if far > near else None


def reference_optical_length(x, y, arm, light):
    """Intersect mathematical solids, subtract cutter intervals, then sum each volume independently."""
    origin = (x, y, 0.0)
    distance = math.sqrt(x * x + y * y + 144.0)
    direction = (-x / distance, -y / distance, -12.0 / distance) if light == LIT_POINT else (0.0, 0.0, -1.0)
    scale = 0.5 if light == LIT_POINT else 1.0
    length = 0.0
    for slot in range(6):
        center_x, center_y = ((slot % 3) - 1) * 2.0 * scale, (1.0 if slot < 3 else -1.0) * scale
        for copy in range(2 if slot == 4 else 1):
            z_shift = -0.5 * copy
            lower = (center_x - 0.7 * scale, center_y - 0.7 * scale, -7.0 + z_shift)
            upper = (center_x + 0.7 * scale, center_y + 0.7 * scale, -5.0 + z_shift)
            solid = ray_box(origin, direction, lower, upper)
            if solid is None:
                continue
            remaining = solid[1] - solid[0]
            if arm != LIT_UNCUT and slot != 5:
                if slot in (0, 3):
                    half_width = 0.36 if light == LIT_POINT else 0.24
                    cut_lower = (center_x + 0.3 * scale - half_width, center_y - 0.4 * scale, -7.2)
                    cut_upper = (center_x + 0.3 * scale + half_width, center_y + 0.4 * scale, -4.8)
                else:
                    cut_lower = (center_x - 1.0, center_y - 1.0, -6.0 + z_shift)
                    cut_upper = (center_x + 1.0, center_y + 1.0, -4.8 + z_shift)
                cut = ray_box(origin, direction, cut_lower, cut_upper)
                if cut:
                    remaining -= max(0.0, min(solid[1], cut[1]) - max(solid[0], cut[0]))
            if remaining > 1e-9 and slot < 2:
                return None
            length += max(remaining, 0.0)
    return length


def expected_sample_rgb(x, y, arm, light):
    # The slab solver already returns Euclidean distance, including the point-light ray angle.
    return expected_rgb(reference_optical_length(x, y, arm, light), x, y, LIT_DIRECTIONAL)


def project_receiver(x, y, width, height, camera_x=0.0):
    # Fixture camera is at z=-4 with the engine's explicit 60-degree vertical FOV and no rotation.
    pixels_per_world = height / (8.0 * math.tan(math.pi / 6.0))
    return round(width * 0.5 + (x - camera_x) * pixels_per_world - 0.5), round(height * 0.5 - y * pixels_per_world - 0.5)


def region_pixels(region, width, height, camera_x=0.0, spacing=0.015):
    _, center_x, center_y = region[:3]
    result = {project_receiver(center_x + ix * spacing, center_y + iy * spacing, width, height, camera_x)
        for iy in range(-3, 4) for ix in range(-3, 4)}
    if any(x < 0 or x >= width or y < 0 or y >= height for x, y in result):
        raise SmokeFailure("CSG shadow reference region is outside the capture")
    return sorted(result)


def measure_frame(frame, arm, light):
    width, height, rows = frame
    if width < 320 or height < 240 or len(rows) != height or any(len(row) != width for row in rows):
        raise SmokeFailure(LIT_CSG_SHADOW_CAPTURE_DIMENSIONS_ARE_INVA)
    camera_x = 0.3 if arm == LIT_CAMERA_SHIFT else 0.0
    measurements = {}
    for region in REGIONS:
        name, world_x, world_y, _, _ = region
        pixels = region_pixels(region, width, height, camera_x)
        rgb = tuple(sum(rows[y][x][channel] for x, y in pixels) / len(pixels) for channel in range(3))
        expected = expected_sample_rgb(world_x, world_y, arm, light)
        error = max(abs(a - b) for a, b in zip(rgb, expected))
        if error > BYTE_TOLERANCE:
            raise SmokeFailure(f"{arm}/{name}: shadow disagrees with carved-box/Beer oracle; RGB={rgb}, expected={expected}, error={error:.3f}")
        measurements[name] = {LIT_RGB: rgb, LIT_EXPECTED_RGB: expected, LIT_MAXIMUM_MEAN_BYTE_ERROR: error,
            LIT_SAMPLE_COUNT: len(pixels)}
    return measurements


def compare_frames(frames, light=LIT_DIRECTIONAL):
    if light not in (LIT_DIRECTIONAL, LIT_POINT) or set(frames) != set(ARMS):
        raise SmokeFailure("CSG shadow comparison requires all five arms and a supported light")
    if len({frame[:2] for frame in frames.values()}) != 1:
        raise SmokeFailure(LIT_CSG_SHADOW_CAPTURES_HAVE_DIFFERENT_EXT)
    measurements = {arm: measure_frame(frames[arm], arm, light) for arm in ARMS}
    reference = measurements[LIT_REFERENCE]
    for arm in (LIT_CUT, LIT_MOVED, LIT_CAMERA_SHIFT):
        for name, expected in reference.items():
            error = max(abs(a - b) for a, b in zip(measurements[arm][name][LIT_RGB], expected[LIT_RGB]))
            if error > BYTE_TOLERANCE:
                raise SmokeFailure(f"{arm}/{name}: cut shadow differs from independent ordinary-box geometry by {error:.3f} bytes")
    for name, minimum_gain in ((LIT_OPAQUE_HOLE, 160.0), (LIT_GLASS_HOLE, 35.0), (LIT_GLASS_DEPTH, 15.0), (LIT_OVERLAP, 15.0)):
        gain = max(a - b for a, b in zip(measurements[LIT_CUT][name][LIT_RGB], measurements[LIT_UNCUT][name][LIT_RGB]))
        if gain < minimum_gain:
            raise SmokeFailure(f"{name}: cut/uncut control has insufficient shadow change ({gain:.3f} bytes)")
    return {LIT_LIGHT: light, LIT_EXTENT: list(frames[LIT_REFERENCE][:2]), LIT_MEAN_RGB_TOLERANCE_BYTES: BYTE_TOLERANCE,
        LIT_ORACLE: "ordinary carved box pieces plus independent Beer law, IOR1, linear presentation and sRGB attachment",
        LIT_REGIONS: measurements}


ANALYTIC_REGION_SPACING = 0.006
ANALYTIC_REGIONS = (
    (LIT_CLEAR, 0.0, 0.0),
    (LIT_PLANE_LEFT, -2.3, 1.0),
    ("plane_right", -1.7, 1.0),
    (LIT_ELLIPSOID_CENTER, 0.0, 1.0),
    ("ellipsoid_offset", 0.25, 1.0),
    ("ellipsoid_rim", 0.61, 1.0),
    (LIT_CAPSULE_BODY, 2.0, 1.0),
    ("capsule_endcap", 2.0, 1.52),
    (LIT_AXIAL_CAPSULE_CENTER, -2.0, -1.0),
    ("axial_capsule_offset", -1.82, -1.0),
    (LIT_SPHERE_UNION_CENTER, 0.0, -1.0),
    ("sphere_union_offset", 0.3, -1.0),
    ("uncut_control", 2.0, -1.0),
)


def clipped_union_length(intervals, lower=-7.0, upper=-5.0):
    intervals = sorted((max(lower, start), min(upper, end)) for start, end in intervals
        if min(upper, end) > max(lower, start))
    total, previous_end = 0.0, lower
    for start, end in intervals:
        total += max(0.0, end - max(start, previous_end))
        previous_end = max(previous_end, end)
    return total


def analytic_optical_length(x, y, arm):
    """World-space cross sections of six glass boxes; no engine transforms or ray solver are reused."""
    for slot in range(6):
        dx, dy = x - ((slot % 3) - 1) * 2.0, y - (1.0 if slot < 3 else -1.0)
        if abs(dx) > 0.7 or abs(dy) > 0.7:
            continue
        if arm == LIT_UNCUT or slot == 5:
            return 2.0
        if slot == 0:
            # Remove 0.5 * dx + (z + 6) <= 0, leaving a tilted depth cut.
            cuts = [(-math.inf, -6.0 - 0.5 * dx)]
        elif slot == 1:
            radial = (dx / 0.5) ** 2 + (dy / 0.45) ** 2
            half_chord = 0.75 * math.sqrt(max(0.0, 1.0 - radial))
            cuts = [(-6.0 - half_chord, -6.0 + half_chord)]
        elif slot == 2:
            # The distance to the transverse Y segment separates body and hemispherical endcaps.
            segment_distance = max(0.0, abs(dy) - 0.25)
            half_chord = math.sqrt(max(0.0, 0.35 ** 2 - dx ** 2 - segment_distance ** 2))
            cuts = [(-6.0 - half_chord, -6.0 + half_chord)]
        elif slot == 3:
            radial_squared = dx ** 2 + dy ** 2
            half_chord = 0.4 + math.sqrt(0.3 ** 2 - radial_squared) if radial_squared <= 0.3 ** 2 else 0.0
            cuts = [(-6.0 - half_chord, -6.0 + half_chord)]
        else:
            half_chord = math.sqrt(max(0.0, 0.55 ** 2 - dx ** 2 - dy ** 2))
            cuts = [(center - half_chord, center + half_chord) for center in (-6.25, -5.75)]
        return 2.0 - clipped_union_length(cuts)
    return 0.0


def receiver_pixel_world(x, y, width, height):
    pixels_per_world = height / (8.0 * math.tan(math.pi / 6.0))
    return (x + 0.5 - width * 0.5) / pixels_per_world, (height * 0.5 - y - 0.5) / pixels_per_world


def measure_analytic_frame(frame, arm):
    width, height, rows = frame
    if width < 320 or height < 240 or len(rows) != height or any(len(row) != width for row in rows):
        raise SmokeFailure(LIT_CSG_SHADOW_CAPTURE_DIMENSIONS_ARE_INVA)
    measurements = {}
    for region in ANALYTIC_REGIONS:
        pixels = region_pixels(region, width, height, spacing=ANALYTIC_REGION_SPACING)
        expected_pixels = []
        for x, y in pixels:
            world_x, world_y = receiver_pixel_world(x, y, width, height)
            length = analytic_optical_length(world_x, world_y, arm)
            expected_pixels.append(expected_rgb(length, world_x, world_y, LIT_DIRECTIONAL))
        rgb = tuple(sum(rows[y][x][channel] for x, y in pixels) / len(pixels) for channel in range(3))
        expected = tuple(sum(value[channel] for value in expected_pixels) / len(pixels) for channel in range(3))
        error = max(abs(a - b) for a, b in zip(rgb, expected))
        if error > BYTE_TOLERANCE:
            raise SmokeFailure(f"{arm}/{region[0]}: shadow disagrees with analytic optical-length/Beer oracle; "
                f"RGB={rgb}, expected={expected}, error={error:.3f}")
        measurements[region[0]] = {LIT_RGB: rgb, LIT_EXPECTED_RGB: expected, LIT_MAXIMUM_MEAN_BYTE_ERROR: error,
            LIT_SAMPLE_COUNT: len(pixels)}
    return measurements


def compare_analytic_frames(frames):
    if set(frames) != set(ANALYTIC_ARMS):
        raise SmokeFailure("CSG analytic comparison requires a cut/uncut pair")
    if len({frame[:2] for frame in frames.values()}) != 1:
        raise SmokeFailure(LIT_CSG_SHADOW_CAPTURES_HAVE_DIFFERENT_EXT)
    measurements = {arm: measure_analytic_frame(frames[arm], arm) for arm in ANALYTIC_ARMS}
    for name in (LIT_PLANE_LEFT, LIT_ELLIPSOID_CENTER, LIT_CAPSULE_BODY, LIT_AXIAL_CAPSULE_CENTER, LIT_SPHERE_UNION_CENTER):
        gain = max(a - b for a, b in zip(measurements[LIT_CUT][name][LIT_RGB], measurements[LIT_UNCUT][name][LIT_RGB]))
        if gain < 15.0:
            raise SmokeFailure(f"{name}: analytic cut/uncut control has insufficient shadow change ({gain:.3f} bytes)")
    return {LIT_LIGHT: LIT_DIRECTIONAL, LIT_EXTENT: list(frames[LIT_CUT][:2]), LIT_MEAN_RGB_TOLERANCE_BYTES: BYTE_TOLERANCE,
        LIT_ORACLE: "independent analytic cross sections and cutter union plus Beer law, IOR1 and sRGB attachment",
        LIT_REGIONS: measurements}
