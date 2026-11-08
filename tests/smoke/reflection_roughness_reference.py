#!/usr/bin/env python3
"""Independent area-integrated GGX reference for the actual reflection smoke image."""

import math

from reflection_image_analysis import ReflectionImageAnalysis
from reflection_roughness_oracle import GRID_SIZE, ReflectionRoughnessOracle
from reflection_smoke import SmokeFailure

LIT_ENERGY = "energy"
LIT_CENTROID = "centroid"
LIT_SPREAD = "spread"
LIT_MAXIMUM_LINEAR_RADIANCE = "maximum_linear_radiance"
LIT_RED = "red"
LIT_GREEN = "green"
LIT_MEASURED = "measured"


def decode_radiance(channel):
    encoded = channel / 255.0
    mapped = encoded / 12.92 if encoded <= 0.04045 else ((encoded + 0.055) / 1.055) ** 2.4
    if mapped >= 0.999:
        raise SmokeFailure("rough reflection reached the clipped presentation range")
    # The fixture pins exposure=1 and the Reinhard shoulder=1 before the sRGB attachment conversion.
    return mapped / (1.0 - mapped)


DECODED_RADIANCE = tuple(decode_radiance(value) for value in range(255))


def frame_grid(frame, camera_x=0.0):
    width, height, rows = ReflectionImageAnalysis.validate_frame(frame)
    cells = ReflectionRoughnessOracle.mirror_cells(width, height, camera_x)
    values = []
    maximum = 0.0
    blue_sum = 0.0
    for left, top, right, bottom in cells:
        totals = [0.0, 0.0, 0.0]
        count = (right - left) * (bottom - top)
        for y in range(top, bottom):
            for x in range(left, right):
                pixel = rows[y][x]
                if any(channel >= 255 for channel in pixel):
                    raise SmokeFailure("rough reflection contains clipped pixels")
                linear = [DECODED_RADIANCE[channel] for channel in pixel]
                maximum = max(maximum, *linear)
                for color in range(3):
                    totals[color] += linear[color]
        values.append((totals[0] / count, totals[1] / count))
        blue_sum += totals[2]
    if maximum > 1.04:
        raise SmokeFailure("rough reflection exceeded the unit-radiance single-scatter energy bound")
    return cells, values, maximum, blue_sum


def weighted_shape(cells, values, color):
    weights = [value[color] * (cell[2] - cell[0]) * (cell[3] - cell[1]) for cell, value in zip(cells, values)]
    total = sum(weights)
    if total <= 0.0:
        raise SmokeFailure("rough reflection lost a colored emitter")
    centers = [((left + right) * 0.5, (top + bottom) * 0.5) for left, top, right, bottom in cells]
    cx = sum(weight * center[0] for weight, center in zip(weights, centers)) / total
    cy = sum(weight * center[1] for weight, center in zip(weights, centers)) / total
    spread = math.sqrt(sum(weight * ((center[0] - cx) ** 2 + (center[1] - cy) ** 2) for weight, center in zip(weights, centers)) / total)
    return {LIT_ENERGY: total, LIT_CENTROID: [cx, cy], LIT_SPREAD: spread}


def analyze_roughness(frame, roughness, error_limit=0.30, camera_x=0.0):
    width, height, _ = ReflectionImageAnalysis.validate_frame(frame)
    cells, actual, maximum, blue_sum = frame_grid(frame, camera_x)
    expected = ReflectionRoughnessOracle.reference_grid(width, height, roughness, camera_x)
    reference_energy = sum(sum(value) for value in expected)
    normalized_error = sum(abs(a - b) for left, right in zip(actual, expected) for a, b in zip(left, right)) / reference_energy
    if normalized_error > error_limit:
        raise SmokeFailure(f"rough reflection differs from the independent GGX area reference ({normalized_error:.4f})")
    result = {"roughness": roughness, "normalized_reference_error": normalized_error, LIT_MAXIMUM_LINEAR_RADIANCE: maximum}
    for color, label in ((0, LIT_RED), (1, LIT_GREEN)):
        measured = weighted_shape(cells, actual, color)
        predicted = weighted_shape(cells, expected, color)
        ratio = measured[LIT_ENERGY] / predicted[LIT_ENERGY]
        if not 0.8 <= ratio <= 1.2:
            raise SmokeFailure(f"{label} rough reflection has incorrect integrated radiance ({ratio:.3f} of reference)")
        if math.dist(measured[LIT_CENTROID], predicted[LIT_CENTROID]) > max(6.0, height * 0.015):
            raise SmokeFailure(f"{label} rough reflection moved away from its predicted GGX position")
        if abs(measured[LIT_SPREAD] - predicted[LIT_SPREAD]) > max(width / GRID_SIZE[0], predicted[LIT_SPREAD] * 0.25):
            raise SmokeFailure(f"{label} rough reflection has the wrong GGX spread")
        result[label] = {LIT_MEASURED: measured, "predicted": predicted, "energy_ratio": ratio}
    if blue_sum > 0.01 * sum(result[color][LIT_MEASURED][LIT_ENERGY] for color in (LIT_RED, LIT_GREEN)):
        raise SmokeFailure("rough reflection imported color absent from the authored emitters")
    return result


def analyze_furnace(frame, roughness):
    if roughness not in (0.0, 1.0):
        raise SmokeFailure("closed-form furnace oracle only supports roughness zero or one")
    width, height, rows = ReflectionImageAnalysis.validate_frame(frame)
    cells, actual, maximum, _ = frame_grid(frame)
    errors = []
    for cell, value in zip(cells, actual):
        point = ReflectionRoughnessOracle.receiver_point((cell[0] + cell[2]) * 0.5, (cell[1] + cell[3]) * 0.5, width, height)
        nv = -ReflectionRoughnessOracle.normalize((-point[0], 1.4 - point[1], -6.0))[2]
        # At alpha=1 and F0=1, correlated Smith single-scatter GGX has this closed-form directional albedo.
        expected = 1.0 if roughness == 0.0 else 1.0 - nv * math.log1p(1.0 / nv)
        pixel_count = (cell[2] - cell[0]) * (cell[3] - cell[1])
        blue = sum(DECODED_RADIANCE[rows[y][x][2]] for y in range(cell[1], cell[3]) for x in range(cell[0], cell[2])) / pixel_count
        errors.extend(abs(channel - expected) for channel in (*value, blue))
    mean_error = sum(errors) / len(errors)
    if mean_error > 0.04:
        raise SmokeFailure(f"white-furnace reflection violates the analytic single-scatter albedo ({mean_error:.4f})")
    return {"mean_linear_albedo_error": mean_error, LIT_MAXIMUM_LINEAR_RADIANCE: maximum}
