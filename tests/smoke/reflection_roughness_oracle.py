"""Compute the independent GGX reflection reference."""

from functools import lru_cache
import math

from reflection_smoke import SmokeFailure

EMITTERS = ((-1.6, 1.4, -8.0), (1.6, 1.4, -8.0))
EMITTER_HALF_SIZE = 0.75
MIRROR_F0 = 0.95
GRID_SIZE = (24, 16)


class ReflectionRoughnessOracle:
    @staticmethod
    def mirror_cells(width, height, camera_x=0.0):
        focal = height / (2.0 * math.tan(math.pi / 6.0))
        left = math.ceil(width * 0.5 + focal * (-2.8 - camera_x) / 6.0) + 2
        right = math.floor(width * 0.5 + focal * (2.8 - camera_x) / 6.0) - 2
        top = math.ceil(height * 0.5 - focal * 1.8 / 6.0) + 2
        bottom = math.floor(height * 0.5 + focal * 1.8 / 6.0) - 2
        nx, ny = GRID_SIZE
        return [(left + (right - left) * x // nx, top + (bottom - top) * y // ny, left + (right - left) * (x + 1) // nx, top + (bottom - top) * (y + 1) // ny) for y in range(ny) for x in range(nx)]

    @staticmethod
    def receiver_point(x, y, width, height, camera_x=0.0):
        focal = height / (2.0 * math.tan(math.pi / 6.0))
        return camera_x + 6.0 * (x - width * 0.5) / focal, 1.4 - 6.0 * (y - height * 0.5) / focal, 0.0

    @staticmethod
    def normalize(vector):
        inverse_length = 1.0 / math.sqrt(sum(value * value for value in vector))
        return tuple(value * inverse_length for value in vector)

    @staticmethod
    def smith_lambda(cosine, alpha):
        return 0.5 * (math.sqrt(1.0 + alpha * alpha * max(0.0, 1.0 - cosine * cosine) / (cosine * cosine)) - 1.0)

    @staticmethod
    @lru_cache(maxsize=8)
    def emitter_quadrature(resolution):
        step = 2.0 * EMITTER_HALF_SIZE / resolution
        return tuple(tuple((cx - EMITTER_HALF_SIZE + (x + 0.5) * step, cy - EMITTER_HALF_SIZE + (y + 0.5) * step, cz, step * step) for y in range(resolution) for x in range(resolution)) for cx, cy, cz in EMITTERS)

    @staticmethod
    def integrated_radiance(point, roughness, color, camera_x=0.0, resolution=24):
        view = ReflectionRoughnessOracle.normalize((camera_x - point[0], 1.4 - point[1], -6.0))
        nv = -view[2]
        if roughness == 0.0:
            # Smooth reflection intersects the emitter plane directly.
            direction = (-view[0], -view[1], view[2])
            distance = -8.0 / direction[2]
            hit_x, hit_y = point[0] + direction[0] * distance, point[1] + direction[1] * distance
            cx, cy, _ = EMITTERS[color]
            inside = abs(hit_x - cx) <= EMITTER_HALF_SIZE and abs(hit_y - cy) <= EMITTER_HALF_SIZE
            return (MIRROR_F0 + (1.0 - MIRROR_F0) * (1.0 - nv) ** 5) if inside else 0.0
        alpha = roughness * roughness
        alpha_squared = alpha * alpha
        lambda_v = ReflectionRoughnessOracle.smith_lambda(nv, alpha)
        total = 0.0
        for sx, sy, sz, area in ReflectionRoughnessOracle.emitter_quadrature(resolution)[color]:
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
            masking = 1.0 / (1.0 + lambda_v + ReflectionRoughnessOracle.smith_lambda(nl, alpha))
            fresnel = MIRROR_F0 + (1.0 - MIRROR_F0) * (1.0 - vh) ** 5
            # Parallel receiver and emitter normals cancel one NdotL against the BRDF denominator.
            total += distribution * masking * fresnel * nl * area / (4.0 * nv * distance_squared)
        return total

    @staticmethod
    @lru_cache(maxsize=16)
    def reference_grid(width, height, roughness, camera_x=0.0):
        if not 0.0 <= roughness <= 1.0:
            raise SmokeFailure("reference roughness must be within [0,1]")
        values = []
        for left, top, right, bottom in ReflectionRoughnessOracle.mirror_cells(width, height, camera_x):
            if roughness == 0.0:
                positions = ((x + 0.5, y + 0.5) for y in range(top, bottom) for x in range(left, right))
                count = (right - left) * (bottom - top)
            else:
                positions = ((left + (right - left) * x, top + (bottom - top) * y) for y in (0.25, 0.75) for x in (0.25, 0.75))
                count = 4
            sums = [0.0, 0.0]
            for x, y in positions:
                point = ReflectionRoughnessOracle.receiver_point(x, y, width, height, camera_x)
                for color in (0, 1):
                    sums[color] += ReflectionRoughnessOracle.integrated_radiance(point, roughness, color, camera_x)
            values.append(tuple(total / count for total in sums))
        return tuple(values)
