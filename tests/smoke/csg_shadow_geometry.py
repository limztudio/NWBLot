#!/usr/bin/env python3
"""Independent carved-box and analytic optical-length oracle."""

import math

LIT_POINT = "point"
LIT_DIRECTIONAL = "directional"
LIT_UNCUT = "uncut"


class CsgShadowGeometry:
    TRANSMISSION = (0.25, 0.5, 0.75)

    @staticmethod
    def expected_rgb(length, world_x, world_y, light):
        if length is None:
            return (0.0, 0.0, 0.0)
        if light == LIT_POINT:
            length *= math.sqrt(12.0 ** 2 + world_x ** 2 + world_y ** 2) / 12.0
        linear = [channel ** length for channel in CsgShadowGeometry.TRANSMISSION]
        return tuple(255.0 * (12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055) for value in linear)

    @staticmethod
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

    @staticmethod
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
                solid = CsgShadowGeometry.ray_box(origin, direction, lower, upper)
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
                    cut = CsgShadowGeometry.ray_box(origin, direction, cut_lower, cut_upper)
                    if cut:
                        remaining -= max(0.0, min(solid[1], cut[1]) - max(solid[0], cut[0]))
                if remaining > 1e-9 and slot < 2:
                    return None
                length += max(remaining, 0.0)
        return length

    @staticmethod
    def expected_sample_rgb(x, y, arm, light):
        # The slab solver already returns Euclidean distance, including the point-light ray angle.
        return CsgShadowGeometry.expected_rgb(CsgShadowGeometry.reference_optical_length(x, y, arm, light), x, y, LIT_DIRECTIONAL)

    @staticmethod
    def clipped_union_length(intervals, lower=-7.0, upper=-5.0):
        intervals = sorted((max(lower, start), min(upper, end)) for start, end in intervals if min(upper, end) > max(lower, start))
        total, previous_end = 0.0, lower
        for start, end in intervals:
            total += max(0.0, end - max(start, previous_end))
            previous_end = max(previous_end, end)
        return total

    @staticmethod
    def analytic_optical_length(x, y, arm):
        """World-space cross sections of six glass boxes; no engine transforms or ray solver are reused."""
        for slot in range(6):
            dx, dy = x - ((slot % 3) - 1) * 2.0, y - (1.0 if slot < 3 else -1.0)
            if abs(dx) > 0.7 or abs(dy) > 0.7:
                continue
            if arm == LIT_UNCUT or slot == 5:
                return 2.0
            if slot == 0:
                # Cut the tilted half-space 0.5 * dx + z + 6 <= 0.
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
            return 2.0 - CsgShadowGeometry.clipped_union_length(cuts)
        return 0.0
