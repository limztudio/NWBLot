"""Projection and image evidence for the refraction density capture."""

import math

from refraction_density_benchmark import DENSITIES, ROUTES
from window_capture_smoke import SmokeFailure

BOUNDARY_GUARD = 1
BLOCK_EDGE = 8
FRESNEL_TOLERANCE = 2.0


class RefractionDensityImageEvidence:
    @staticmethod
    def projected_bounds(width, height, percent):
        if percent not in DENSITIES:
            raise SmokeFailure("unsupported density percentage")
        if not percent:
            return 0, 0, 0, 0
        fraction = math.sqrt(percent / 100)
        covered_width = min(width, 2 * int(width * fraction / 2 + .5))
        covered_height = min(height, 2 * int(height * fraction / 2 + .5))
        left, top = (width - covered_width) // 2, (height - covered_height) // 2
        return left, top, left + covered_width, top + covered_height

    @staticmethod
    def validate_backdrop(frame):
        width, height, rows = frame
        dark = light = bad_color = varying = 0
        for row in rows:
            for x, pixel in enumerate(row):
                neutral = max(pixel) - min(pixel) <= 1
                value = sum(pixel) / 3
                dark += neutral and abs(value - 68) <= 1
                light += neutral and abs(value - 201) <= 1
                bad_color += not neutral or min(abs(value - 68), abs(value - 201)) > 1
                varying += max(abs(a - b) for a, b in zip(pixel, rows[0][x])) > 1
        total = width * height
        if bad_color > max(8, total * .001) or varying > max(8, total * .001) or min(dark, light) < total * .3:
            raise SmokeFailure(f"zero-density backdrop is not the neutral unlit reference: dark={dark}, light={light}, bad_color={bad_color}, vertical_changes={varying}")
        return {"pixels": total, "dark_pixels": dark, "light_pixels": light, "unexpected_color_pixels": bad_color, "vertical_variation_pixels": varying}

    @staticmethod
    def encoded_transmission(level, transmission):
        encoded = level / 255
        linear = encoded / 12.92 if encoded <= .04045 else ((encoded + .055) / 1.055) ** 2.4
        # Inverting then reapplying Reinhard cancels its common exposure/shoulder scale.
        mapped = transmission * linear / (1 - linear + transmission * linear)
        return 255 * (12.92 * mapped if mapped <= .0031308 else 1.055 * mapped ** (1 / 2.4) - .055)

    @staticmethod
    def expected_levels(width, height, x, y, route):
        tangent = math.tan(math.pi / 6)
        ray_x = (2 * (x + .5) / width - 1) * tangent * width / height
        ray_y = (1 - 2 * (y + .5) / height) * tangent
        entry_cosine = 1 / math.sqrt(1 + ray_x * ray_x + ray_y * ray_y)
        exit_cosine = math.sqrt(1 - (1 - entry_cosine * entry_cosine) / 2.25) if route == "hardware" else entry_cosine
        entry_transmission = 1 - (.04 + .96 * (1 - entry_cosine) ** 5)
        exit_transmission = 1 - (.04 + .96 * (1 - exit_cosine) ** 5)
        transmission = entry_transmission * exit_transmission
        return RefractionDensityImageEvidence.encoded_transmission(68, transmission), RefractionDensityImageEvidence.encoded_transmission(201, transmission)

    @staticmethod
    def physical_core_guard(width, height, bounds):
        left, top, right, bottom = bounds
        tangent = math.tan(math.pi / 6)
        ray_x = max(abs(2 * left / width - 1), abs(2 * right / width - 1)) * tangent * width / height
        ray_y = max(abs(1 - 2 * top / height), abs(1 - 2 * bottom / height)) * tangent
        incident_length = math.sqrt(1 + ray_x * ray_x + ray_y * ray_y)
        inside_z = math.sqrt(1 - (ray_x * ray_x + ray_y * ray_y) / (2.25 * incident_length * incident_length))
        focal_pixels = height / (2 * tangent)
        # Exclude slab side-wall exits from the flat Fresnel check.
        return max(2, math.ceil(.2 * max(ray_x, ray_y) / (1.5 * incident_length * inside_z) * focal_pixels / 6) + 2)

    @staticmethod
    def analyze_density_frame(reference, candidate, percent, route):
        width, height, rows = reference
        if candidate[:2] != (width, height):
            raise SmokeFailure("density framebuffer extent differs from its zero-density reference")
        if route not in ROUTES or percent not in DENSITIES or percent == 0:
            raise SmokeFailure("density image comparison requires a positive supported density and an explicit route")
        left, top, right, bottom = RefractionDensityImageEvidence.projected_bounds(width, height, percent)
        inner = left + BOUNDARY_GUARD, top + BOUNDARY_GUARD, right - BOUNDARY_GUARD, bottom - BOUNDARY_GUARD
        core_guard = RefractionDensityImageEvidence.physical_core_guard(width, height, (left, top, right, bottom))
        core = left + core_guard, top + core_guard, right - core_guard, bottom - core_guard
        blocks_x = (inner[2] - inner[0] + BLOCK_EDGE - 1) // BLOCK_EDGE
        blocks_y = (inner[3] - inner[1] + BLOCK_EDGE - 1) // BLOCK_EDGE
        block_tested, block_changed = [0] * (blocks_x * blocks_y), [0] * (blocks_x * blocks_y)
        block_core_tested, block_core_valid = [0] * (blocks_x * blocks_y), [0] * (blocks_x * blocks_y)
        changed = interior = interior_changed = outside_changed = 0
        core_pixels = core_valid = dark = light = 0
        changed_left, changed_top, changed_right, changed_bottom = width, height, 0, 0
        for y, (before_row, after_row) in enumerate(zip(rows, candidate[2])):
            for x, (before, after) in enumerate(zip(before_row, after_row)):
                different = before != after
                if different:
                    changed += 1
                    changed_left, changed_top = min(changed_left, x), min(changed_top, y)
                    changed_right, changed_bottom = max(changed_right, x + 1), max(changed_bottom, y + 1)
                    if not (left - BOUNDARY_GUARD <= x < right + BOUNDARY_GUARD and top - BOUNDARY_GUARD <= y < bottom + BOUNDARY_GUARD):
                        outside_changed += 1
                if inner[0] <= x < inner[2] and inner[1] <= y < inner[3]:
                    interior += 1
                    interior_changed += different
                    block = ((y - inner[1]) // BLOCK_EDGE) * blocks_x + (x - inner[0]) // BLOCK_EDGE
                    block_tested[block] += 1
                    block_changed[block] += different
                if core[0] <= x < core[2] and core[1] <= y < core[3]:
                    core_pixels += 1
                    expected_dark, expected_light = RefractionDensityImageEvidence.expected_levels(width, height, x, y, route)
                    value = sum(after) / 3
                    is_dark = abs(value - expected_dark) <= FRESNEL_TOLERANCE
                    is_light = abs(value - expected_light) <= FRESNEL_TOLERANCE
                    valid = max(after) - min(after) <= 1 and min(after) > 0 and (is_dark or is_light)
                    core_valid += valid
                    dark += valid and is_dark
                    light += valid and is_light
                    block = ((y - inner[1]) // BLOCK_EDGE) * blocks_x + (x - inner[0]) // BLOCK_EDGE
                    block_core_tested[block] += 1
                    block_core_valid[block] += valid
        actual_bounds = [changed_left, changed_top, changed_right, changed_bottom] if changed else None
        if outside_changed:
            raise SmokeFailure(f"density output changed outside its projected mask: {outside_changed} pixels, bounds={actual_bounds}")
        if not actual_bounds or any(abs(observed - expected) > BOUNDARY_GUARD for observed, expected in zip(actual_bounds, (left, top, right, bottom))):
            raise SmokeFailure(f"density changed bounds miss the projected rectangle: observed={actual_bounds}, expected={(left, top, right, bottom)}")
        if not interior or interior_changed < interior * .99:
            raise SmokeFailure(f"density interior is incomplete or unchanged: {interior_changed}/{interior} pixels")
        bad_blocks = sum(tested >= 16 and count < tested * .9 for tested, count in zip(block_tested, block_changed))
        if bad_blocks:
            raise SmokeFailure(f"density has missing local coverage: {bad_blocks} incomplete 8x8 blocks")
        if not core_pixels or core_valid < core_pixels * .98 or min(dark, light) < max(12, core_pixels * .05):
            raise SmokeFailure(f"density core lost neutral nonblack Fresnel stripes: accepted={core_valid}/{core_pixels}, dark={dark}, light={light}, core_guard={core_guard}")
        bad_core_blocks = sum(tested >= 16 and count < tested * .9 for tested, count in zip(block_core_tested, block_core_valid))
        if bad_core_blocks:
            raise SmokeFailure(f"density has locally invalid Fresnel output: {bad_core_blocks} incomplete 8x8 core blocks")
        expected_pixels = (right - left) * (bottom - top)
        return {"requested_percent": percent, "analytical_bounds": [left, top, right, bottom], "analytical_pixels": expected_pixels,
            "actual_changed_bounds": actual_bounds, "actual_changed_pixels": changed, "actual_changed_fraction": changed / (width * height),
            "changed_minus_analytical_pixels": changed - expected_pixels, "boundary_guard_pixels": BOUNDARY_GUARD, "outside_changed_pixels": outside_changed,
            "interior_tested_pixels": interior, "interior_changed_pixels": interior_changed, "tested_local_blocks": sum(count >= 16 for count in block_tested),
            "physical_core_guard_pixels": core_guard, "physical_core_pixels": core_pixels, "physical_core_accepted_pixels": core_valid,
            "physical_core_dark_pixels": dark, "physical_core_light_pixels": light, "fresnel_encoded_tolerance": FRESNEL_TOLERANCE}
