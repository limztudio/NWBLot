"""Visible receiver footprint analysis for caustic qualification."""

import math

from window_capture_outcome import SmokeFailure

LIT_POPULATED = "populated"
LIT_SPARSE = "sparse"
LIT_CAMERA_HEIGHT = "camera_height"
LIT_VERTICAL_FOV_DEGREES = "vertical_fov_degrees"
LIT_SPHERE_RADIUS = "sphere_radius"
LIT_SPHERE_EXCLUSION_MARGIN = "sphere_exclusion_margin"
LIT_GROUND_Y = "ground_y"
LIT_GROUND_X = "ground_x"
LIT_GROUND_Z = "ground_z"
LIT_RECEIVER_EDGE_MARGIN = "receiver_edge_margin"
LIT_POSITIVE_CHANNEL_SUM_THRESHOLD = "positive_channel_sum_threshold"
LIT_MINIMUM_POSITIVE_PIXELS = "minimum_positive_pixels"
LIT_MINIMUM_CHANNEL_GAIN = "minimum_channel_gain"
LIT_TILE_EXTENT = "tile_extent"
LIT_MAXIMUM_SPARSE_PIXEL_RATIO = "maximum_sparse_pixel_ratio"
LIT_MAXIMUM_SPARSE_TILE_RATIO = "maximum_sparse_tile_ratio"
LIT_POSITIVE_PIXELS = "positive_pixels"
LIT_POSITIVE_CHANNEL_GAIN = "positive_channel_gain"
LIT_POSITIVE_TILES = "positive_tiles"


class CausticFootprint:
    def __init__(self, policy, presets):
        self.policy = policy
        self.presets = presets

    def receiver_pixels(self, width, height, distance):
        """Pinhole ray/plane test excludes a conservative enclosing sphere and ground edges."""
        tangent = math.tan(math.radians(self.policy[LIT_VERTICAL_FOV_DEGREES]) * .5)
        for y in range(height // 2, height):
            dy = (1 - 2 * (y + .5) / height) * tangent
            if dy >= 0:
                continue
            plane_t = (self.policy[LIT_GROUND_Y] - self.policy[LIT_CAMERA_HEIGHT]) / dy
            z = -distance + plane_t
            margin = self.policy[LIT_RECEIVER_EDGE_MARGIN]
            if not self.policy[LIT_GROUND_Z][0] + margin < z < self.policy[LIT_GROUND_Z][1] - margin:
                continue
            for x in range(width):
                dx = (2 * (x + .5) / width - 1) * tangent * width / height
                if not self.policy[LIT_GROUND_X][0] + margin < dx * plane_t < self.policy[LIT_GROUND_X][1] - margin:
                    continue
                # Quadratic roots parameterize unnormalized ray direction.
                a = dx * dx + dy * dy + 1
                exclusion_radius = self.policy[LIT_SPHERE_RADIUS] + self.policy[LIT_SPHERE_EXCLUSION_MARGIN]
                discriminant = distance * distance - a * (distance * distance - exclusion_radius ** 2)
                if discriminant >= 0:
                    entry = (distance - math.sqrt(discriminant)) / a
                    if 0 < entry < plane_t:
                        continue
                yield x, y

    def footprint(self, on, off, distance):
        width, height, rows = on
        if off[:2] != on[:2] or len(rows) != height or len(off[2]) != height \
            or any(len(row) != width for row in (*rows, *off[2])):
            raise SmokeFailure("caustic footprint images have inconsistent shapes")
        receiver_count = changed = gain = 0
        tiles = set()
        for x, y in self.receiver_pixels(width, height, distance):
            receiver_count += 1
            delta = sum(a - b for a, b in zip(rows[y][x], off[2][y][x]))
            if delta > self.policy[LIT_POSITIVE_CHANNEL_SUM_THRESHOLD]:
                changed += 1
                gain += delta
                tiles.add((x // self.policy[LIT_TILE_EXTENT], y // self.policy[LIT_TILE_EXTENT]))
        return {"receiver_pixels": receiver_count, LIT_POSITIVE_PIXELS: changed, LIT_POSITIVE_CHANNEL_GAIN: gain, LIT_POSITIVE_TILES: len(tiles)}

    def validate_metrics(self, metrics):
        if set(metrics) != set(self.presets):
            raise SmokeFailure("both populated and sparse camera captures are required")
        for value in metrics.values():
            if value[LIT_POSITIVE_PIXELS] < self.policy[LIT_MINIMUM_POSITIVE_PIXELS] \
                or value[LIT_POSITIVE_CHANNEL_GAIN] < self.policy[LIT_MINIMUM_CHANNEL_GAIN]:
                raise SmokeFailure("caustic contribution has insufficient actual positive receiver coverage")
        populated, sparse = metrics[LIT_POPULATED], metrics[LIT_SPARSE]
        if sparse[LIT_POSITIVE_PIXELS] > populated[LIT_POSITIVE_PIXELS] * self.policy[LIT_MAXIMUM_SPARSE_PIXEL_RATIO] \
            or sparse[LIT_POSITIVE_TILES] > populated[LIT_POSITIVE_TILES] * self.policy[LIT_MAXIMUM_SPARSE_TILE_RATIO]:
            raise SmokeFailure("camera pair did not qualify distinct populated/sparse visible footprints")

