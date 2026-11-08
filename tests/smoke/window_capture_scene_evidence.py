"""Image evidence for transparent, texture and surfel smoke scenes."""


from dataclasses import dataclass

from window_capture_image_codec import Bmp24Codec
from window_capture_image_regions import ImageRegions
from window_capture_outcome import SmokeFailure


LIT_FRONT = "front"
LIT_SIDE = "side"
# Receiver shadow region below the sphere where red light comes only from indirect texture bounce.
TEXTURE_SMOKE_RECEIVER_REGION = (0.375, 0.667, 0.438, 0.700)
TEXTURE_SMOKE_RECEIVER_MIN_RED = 36
TEXTURE_SMOKE_RECEIVER_RED_CHROMA = 6
# Center-relative cut and retained sample rectangles for CSG yaw poses 0, 1, and 2.
TRANSPARENT_CSG_REGIONS = { "early": ((-.030, -.125, .030, -.065), (-.030, .065, .030, .125)), "mid": ((.025, -.125, .065, -.080), (-.065, .110, -.025, .140)), "late": ((-.055, -.128, -.025, -.104), (.045, -.075, .080, -.050)), }


@dataclass(frozen=True)
class TransparentMultiAnalysis:
    left_pixels: int
    center_pixels: int
    right_pixels: int


@dataclass(frozen=True)
class TransparentCsgAnalysis:
    cut_void_pixels: int
    cut_region_pixels: int
    remaining_center_pixels: int
    remaining_region_pixels: int


@dataclass(frozen=True)
class TextureSmokeAnalysis:
    red_pixels: int
    green_pixels: int
    blue_pixels: int
    receiver_red_pixels: int
    receiver_pixel_count: int


class SurfelCoverageImageEvidence:
    @staticmethod
    def validate_surfel_coverage_probe(path):
        """Check the interior of both same-cell, perpendicular faces in the opt-in GI scene."""
        width, height, rows = Bmp24Codec.read_bmp_24_rows(path)
        # Fractional interior regions for the two perpendicular faces of the GI probe cube.
        regions = { LIT_FRONT: (0.80, 0.42, 0.92, 0.55), LIT_SIDE: (0.735, 0.42, 0.770, 0.55), }
        observed = {}
        for face, (left, top, right, bottom) in regions.items():
            x0, x1 = int(left * width), int(right * width)
            y0, y1 = int(top * height), int(bottom * height)
            pixels = 0
            covered = 0
            for row in rows[y0:y1]:
                for red, green, blue in row[x0:x1]:
                    if red + green < 100:
                        continue
                    if face == LIT_FRONT and blue > 40:
                        continue
                    if face == LIT_SIDE and (blue < 140 or blue < max(red, green) * 0.65):
                        continue
                    pixels += 1
                    if red > green + 20:
                        covered += 1
            observed[face] = (covered, pixels)

        minimum = {LIT_FRONT: 1000, LIT_SIDE: 300}
        for face, (covered, pixels) in observed.items():
            if pixels < minimum[face] or covered < pixels * 0.80:
                raise SmokeFailure("surfel coverage probe did not cover the perpendicular faces " f"(front={observed[LIT_FRONT][0]}/{observed[LIT_FRONT][1]}, " f"side={observed[LIT_SIDE][0]}/{observed[LIT_SIDE][1]})")
        return observed



class TransparentImageEvidence:
    @staticmethod
    def analyze_transparent_multi_rows(rows_rgb):
        height = len(rows_rgb)
        width = len(rows_rgb[0]) if height else 0
        if width == 0 or height == 0:
            return TransparentMultiAnalysis(0, 0, 0)

        background = ImageRegions.estimate_background_rgb(rows_rgb, width, height)

        left_pixels = ImageRegions.count_foreground_pixels(rows_rgb, width, height, background, 0.16, 0.30, 0.43, 0.70)
        center_pixels = ImageRegions.count_foreground_pixels(rows_rgb, width, height, background, 0.34, 0.27, 0.58, 0.70)
        right_nominal_pixels = ImageRegions.count_foreground_pixels(rows_rgb, width, height, background, 0.62, 0.30, 0.90, 0.70)
        # The CSG smoke samples a late rotating pose where the right transparent mesh crosses the center/right boundary.
        right_rotated_pixels = ImageRegions.count_foreground_pixels(rows_rgb, width, height, background, 0.47, 0.28, 0.66, 0.64)
        right_pixels = max(right_nominal_pixels, right_rotated_pixels)
        return TransparentMultiAnalysis(left_pixels, center_pixels, right_pixels)


    @staticmethod
    def transparent_csg_regions(width, height, pose):
        def to_pixels(region):
            x0, y0, x1, y1 = region
            return ImageRegions.normalized_region(width, height, .5 + x0 * height / width, .5 + y0, .5 + x1 * height / width, .5 + y1,)

        return tuple(to_pixels(region) for region in TRANSPARENT_CSG_REGIONS[pose])


    @staticmethod
    def analyze_transparent_csg_rows(rows_rgb, pose):
        height = len(rows_rgb)
        width = len(rows_rgb[0]) if height else 0
        if width == 0 or height == 0:
            return TransparentCsgAnalysis(0, 0, 0, 0)

        background = ImageRegions.estimate_background_rgb(rows_rgb, width, height)
        cut_region, remaining_region = TransparentImageEvidence.transparent_csg_regions(width, height, pose)
        # Require both foreground contrast and green dominance in the central CSG receiver.
        remaining_center_pixels = ImageRegions.count_foreground_pixels_in_region(rows_rgb, background, remaining_region, pixel_predicate=lambda rgb: rgb[1] > rgb[0] and rgb[1] > rgb[2],)
        return TransparentCsgAnalysis(ImageRegions.count_background_like_pixels(rows_rgb, background, cut_region), ImageRegions.region_pixel_count(cut_region), remaining_center_pixels, ImageRegions.region_pixel_count(remaining_region),)



class TextureSmokeImageEvidence:
    @staticmethod
    def analyze_texture_smoke_rows(rows_rgb):
        red_pixels = 0
        green_pixels = 0
        blue_pixels = 0
        height = len(rows_rgb)
        width = len(rows_rgb[0]) if height else 0
        receiver_region = ImageRegions.normalized_region(width, height, *TEXTURE_SMOKE_RECEIVER_REGION)
        receiver_red_pixels = 0
        for row in rows_rgb:
            for red, green, blue in row:
                if red >= 72 and red >= green + 34 and red >= blue + 34:
                    red_pixels += 1
                if green >= 72 and green >= red + 34 and green >= blue + 34:
                    green_pixels += 1
                if blue >= 72 and blue >= red + 34 and blue >= green + 34:
                    blue_pixels += 1

        start_x, start_y, end_x, end_y = receiver_region
        for y in range(start_y, end_y):
            for red, green, blue in rows_rgb[y][start_x:end_x]:
                if (red >= TEXTURE_SMOKE_RECEIVER_MIN_RED and red >= green + TEXTURE_SMOKE_RECEIVER_RED_CHROMA and red >= blue + TEXTURE_SMOKE_RECEIVER_RED_CHROMA):
                    receiver_red_pixels += 1

        return TextureSmokeAnalysis(red_pixels, green_pixels, blue_pixels, receiver_red_pixels, ImageRegions.region_pixel_count(receiver_region),)


