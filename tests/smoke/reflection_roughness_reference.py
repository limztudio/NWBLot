#!/usr/bin/env python3
"""Independent area-integrated GGX reference for the actual reflection smoke image."""

from functools import lru_cache
import math

from reflection_smoke import SmokeFailure, validate_frame


CAMERA = (0.0, 1.4, -6.0)
EMITTERS = ((-1.6, 1.4, -8.0), (1.6, 1.4, -8.0))
EMITTER_HALF_SIZE = 0.75
MIRROR_F0 = 0.95
GRID_SIZE = (24, 16)


def decode_radiance(channel):
    encoded = channel / 255.0
    mapped = encoded / 12.92 if encoded <= 0.04045 else ((encoded + 0.055) / 1.055) ** 2.4
    if mapped >= 0.999:
        raise SmokeFailure("rough reflection reached the clipped presentation range")
    # The fixture pins exposure=1 and the Reinhard shoulder=1 before the sRGB attachment conversion.
    return mapped / (1.0 - mapped)


DECODED_RADIANCE = tuple(decode_radiance(value) for value in range(255))


def mirror_cells(width, height, camera_x=0.0):
    focal = height / (2.0 * math.tan(math.pi / 6.0))
    left = math.ceil(width * 0.5 + focal * (-2.8 - camera_x) / 6.0) + 2
    right = math.floor(width * 0.5 + focal * (2.8 - camera_x) / 6.0) - 2
    top = math.ceil(height * 0.5 - focal * 1.8 / 6.0) + 2
    bottom = math.floor(height * 0.5 + focal * 1.8 / 6.0) - 2
    nx, ny = GRID_SIZE
    return [(left + (right - left) * x // nx, top + (bottom - top) * y // ny,
        left + (right - left) * (x + 1) // nx, top + (bottom - top) * (y + 1) // ny)
        for y in range(ny) for x in range(nx)]


def receiver_point(x, y, width, height, camera_x=0.0):
    focal = height / (2.0 * math.tan(math.pi / 6.0))
    return camera_x + 6.0 * (x - width * 0.5) / focal, 1.4 - 6.0 * (y - height * 0.5) / focal, 0.0


def normalize(vector):
    inverse_length = 1.0 / math.sqrt(sum(value * value for value in vector))
    return tuple(value * inverse_length for value in vector)


def smith_lambda(cosine, alpha):
    return 0.5 * (math.sqrt(1.0 + alpha * alpha * max(0.0, 1.0 - cosine * cosine) / (cosine * cosine)) - 1.0)


@lru_cache(maxsize=8)
def emitter_quadrature(resolution):
    step = 2.0 * EMITTER_HALF_SIZE / resolution
    return tuple(tuple((cx - EMITTER_HALF_SIZE + (x + 0.5) * step,
        cy - EMITTER_HALF_SIZE + (y + 0.5) * step, cz, step * step)
        for y in range(resolution) for x in range(resolution)) for cx, cy, cz in EMITTERS)


def integrated_radiance(point, roughness, color, camera_x=0.0, resolution=24):
    view = normalize((camera_x - point[0], 1.4 - point[1], -6.0))
    nv = -view[2]
    if roughness == 0.0:
        # Exact delta reflection intersects the emitter plane; this path has no quadrature singularity.
        direction = (-view[0], -view[1], view[2])
        distance = -8.0 / direction[2]
        hit_x, hit_y = point[0] + direction[0] * distance, point[1] + direction[1] * distance
        cx, cy, _ = EMITTERS[color]
        inside = abs(hit_x - cx) <= EMITTER_HALF_SIZE and abs(hit_y - cy) <= EMITTER_HALF_SIZE
        return (MIRROR_F0 + (1.0 - MIRROR_F0) * (1.0 - nv) ** 5) if inside else 0.0
    alpha = roughness * roughness
    alpha_squared = alpha * alpha
    lambda_v = smith_lambda(nv, alpha)
    total = 0.0
    for sx, sy, sz, area in emitter_quadrature(resolution)[color]:
        lx, ly, lz = sx - point[0], sy - point[1], sz
        distance_squared = lx * lx + ly * ly + lz * lz
        inverse_distance = 1.0 / math.sqrt(distance_squared)
        lx, ly, lz = lx * inverse_distance, ly * inverse_distance, lz * inverse_distance
        nl = -lz
        hx, hy, hz = view[0] + lx, view[1] + ly, view[2] + lz
        inverse_half_length = 1.0 / math.sqrt(hx * hx + hy * hy + hz * hz)
        nh = -hz * inverse_half_length
        vh = (view[0] * hx + view[1] * hy + view[2] * hz) * inverse_half_length
        denominator = nh * nh * (alpha_squared - 1.0) + 1.0
        distribution = alpha_squared / (math.pi * denominator * denominator)
        masking = 1.0 / (1.0 + lambda_v + smith_lambda(nl, alpha))
        fresnel = MIRROR_F0 + (1.0 - MIRROR_F0) * (1.0 - vh) ** 5
        # f_r * NdotL * emitterCosine / distance^2 * dA. The two planes have parallel normals,
        # so emitterCosine=NdotL; one NdotL cancels the BRDF denominator. No VNDF sampler is used.
        total += distribution * masking * fresnel * nl * area / (4.0 * nv * distance_squared)
    return total


@lru_cache(maxsize=16)
def reference_grid(width, height, roughness, camera_x=0.0):
    if not 0.0 <= roughness <= 1.0:
        raise SmokeFailure("reference roughness must be within [0,1]")
    values = []
    for left, top, right, bottom in mirror_cells(width, height, camera_x):
        if roughness == 0.0:
            positions = ((x + 0.5, y + 0.5) for y in range(top, bottom) for x in range(left, right))
            count = (right - left) * (bottom - top)
        else:
            positions = ((left + (right - left) * x, top + (bottom - top) * y)
                for y in (0.25, 0.75) for x in (0.25, 0.75))
            count = 4
        sums = [0.0, 0.0]
        for x, y in positions:
            point = receiver_point(x, y, width, height, camera_x)
            for color in (0, 1):
                sums[color] += integrated_radiance(point, roughness, color, camera_x)
        values.append(tuple(total / count for total in sums))
    return tuple(values)


def frame_grid(frame, camera_x=0.0):
    width, height, rows = validate_frame(frame)
    cells = mirror_cells(width, height, camera_x)
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
    spread = math.sqrt(sum(weight * ((center[0] - cx) ** 2 + (center[1] - cy) ** 2)
        for weight, center in zip(weights, centers)) / total)
    return {"energy": total, "centroid": [cx, cy], "spread": spread}


def analyze_roughness(frame, roughness, error_limit=0.30, camera_x=0.0):
    width, height, _ = validate_frame(frame)
    cells, actual, maximum, blue_sum = frame_grid(frame, camera_x)
    expected = reference_grid(width, height, roughness, camera_x)
    reference_energy = sum(sum(value) for value in expected)
    normalized_error = sum(abs(a - b) for left, right in zip(actual, expected) for a, b in zip(left, right)) / reference_energy
    if normalized_error > error_limit:
        raise SmokeFailure(f"rough reflection differs from the independent GGX area reference ({normalized_error:.4f})")
    result = {"roughness": roughness, "normalized_reference_error": normalized_error, "maximum_linear_radiance": maximum}
    for color, label in ((0, "red"), (1, "green")):
        measured = weighted_shape(cells, actual, color)
        predicted = weighted_shape(cells, expected, color)
        ratio = measured["energy"] / predicted["energy"]
        if not 0.8 <= ratio <= 1.2:
            raise SmokeFailure(f"{label} rough reflection has incorrect integrated radiance ({ratio:.3f} of reference)")
        if math.dist(measured["centroid"], predicted["centroid"]) > max(6.0, height * 0.015):
            raise SmokeFailure(f"{label} rough reflection moved away from its predicted GGX position")
        if abs(measured["spread"] - predicted["spread"]) > max(width / GRID_SIZE[0], predicted["spread"] * 0.25):
            raise SmokeFailure(f"{label} rough reflection has the wrong GGX spread")
        result[label] = {"measured": measured, "predicted": predicted, "energy_ratio": ratio}
    if blue_sum > 0.01 * sum(result[color]["measured"]["energy"] for color in ("red", "green")):
        raise SmokeFailure("rough reflection imported color absent from the authored emitters")
    return result


def analyze_furnace(frame, roughness):
    if roughness not in (0.0, 1.0):
        raise SmokeFailure("closed-form furnace oracle only supports roughness zero or one")
    width, height, rows = validate_frame(frame)
    cells, actual, maximum, _ = frame_grid(frame)
    errors = []
    for cell, value in zip(cells, actual):
        point = receiver_point((cell[0] + cell[2]) * 0.5, (cell[1] + cell[3]) * 0.5, width, height)
        nv = -normalize((-point[0], 1.4 - point[1], -6.0))[2]
        # At alpha=1 and F0=1, correlated Smith single-scatter GGX has this closed-form directional albedo.
        expected = 1.0 if roughness == 0.0 else 1.0 - nv * math.log1p(1.0 / nv)
        pixel_count = (cell[2] - cell[0]) * (cell[3] - cell[1])
        blue = sum(DECODED_RADIANCE[rows[y][x][2]]
            for y in range(cell[1], cell[3]) for x in range(cell[0], cell[2])) / pixel_count
        errors.extend(abs(channel - expected) for channel in (*value, blue))
    mean_error = sum(errors) / len(errors)
    if mean_error > 0.04:
        raise SmokeFailure(f"white-furnace reflection violates the analytic single-scatter albedo ({mean_error:.4f})")
    return {"mean_linear_albedo_error": mean_error, "maximum_linear_radiance": maximum}
