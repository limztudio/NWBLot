"""Compare production reflection spatial-owner framebuffers."""

from reflection_roughness_oracle import ReflectionRoughnessOracle
from reflection_spatial_owner_evidence import LIT_SEQUENCE, SELECTIONS
from window_capture_smoke import SmokeFailure

LIT_EXACT_PAIRS = "exact_pairs"
LIT_RADIUS_DISCRIMINATION = "radius_discrimination"
LIT_FRESH = "fresh"


class ReflectionSpatialOwnerImageEvidence:
    @staticmethod
    def compare_images(frames):
        if set(frames) != set(SELECTIONS):
            raise SmokeFailure("all six fresh/sequence captures are required")
        sizes = {frame[:2] for frame in frames.values()}
        if len(sizes) != 1:
            raise SmokeFailure("owner captures have different dimensions")
        width, height = next(iter(sizes))
        result = {LIT_EXACT_PAIRS: {}, LIT_RADIUS_DISCRIMINATION: {}}
        for radius in (1, 2, 3):
            fresh, sequence = frames[LIT_FRESH + str(radius)], frames[LIT_SEQUENCE + str(radius)]
            if fresh != sequence:
                raise SmokeFailure("radius transition differs from the matching fresh first-sample framebuffer")
            result[LIT_EXACT_PAIRS][str(radius)] = {"rgb_byte_identical": True, "pixels": width * height}
        cells = ReflectionRoughnessOracle.mirror_cells(width, height)
        left, top = cells[0][:2]
        right, bottom = cells[-1][2:]
        area = (right - left) * (bottom - top)
        if area <= 0:
            raise SmokeFailure("empty projected mirror footprint")
        for first, second in ((1, 2), (1, 3), (2, 3)):
            a, b = frames[LIT_FRESH + str(first)][2], frames[LIT_FRESH + str(second)][2]
            changed = sum(max(abs(x - y) for x, y in zip(a[row][column], b[row][column])) > 2 for row in range(top, bottom) for column in range(left, right))
            minimum = max(32, int(area * .001))
            if changed < minimum:
                raise SmokeFailure("fresh radius references do not discriminate wrong-radius bank selection")
            result[LIT_RADIUS_DISCRIMINATION][str(first) + "_" + str(second)] = { "projected_mirror_pixels": area, "pixels_differing_by_more_than_two": changed, "minimum_discriminating_pixels": minimum}
        return result

