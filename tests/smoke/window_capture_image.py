"""Window-capture image IO and analysis: 24-bit BMP read/write, foreground and region pixel statistics, and the smoke-specific image oracles."""


from dataclasses import dataclass
from window_capture_image_codec import Bmp24Codec, NativePixelCodec
from window_capture_image_regions import ImageRegions
from window_capture_scene_evidence import (
    LIT_FRONT, LIT_SIDE, TEXTURE_SMOKE_RECEIVER_REGION, TEXTURE_SMOKE_RECEIVER_MIN_RED,
    TEXTURE_SMOKE_RECEIVER_RED_CHROMA, TRANSPARENT_CSG_REGIONS, TransparentMultiAnalysis,
    TransparentCsgAnalysis, TextureSmokeAnalysis, SurfelCoverageImageEvidence,
    TransparentImageEvidence, TextureSmokeImageEvidence,
)


estimate_background_rgb = ImageRegions.estimate_background_rgb
normalized_region = ImageRegions.normalized_region
region_pixel_count = ImageRegions.region_pixel_count
count_foreground_pixels = ImageRegions.count_foreground_pixels
count_foreground_pixels_in_region = ImageRegions.count_foreground_pixels_in_region
count_background_like_pixels = ImageRegions.count_background_like_pixels

validate_surfel_coverage_probe = SurfelCoverageImageEvidence.validate_surfel_coverage_probe
analyze_transparent_multi_rows = TransparentImageEvidence.analyze_transparent_multi_rows
transparent_csg_regions = TransparentImageEvidence.transparent_csg_regions
analyze_transparent_csg_rows = TransparentImageEvidence.analyze_transparent_csg_rows
analyze_texture_smoke_rows = TextureSmokeImageEvidence.analyze_texture_smoke_rows

write_bmp_24 = Bmp24Codec.write_bmp_24
read_bmp_24_rows = Bmp24Codec.read_bmp_24_rows
channel_from_mask = NativePixelCodec.channel_from_mask
ximage_rgb_rows = NativePixelCodec.ximage_rgb_rows
bgra_rgb_rows = NativePixelCodec.bgra_rgb_rows


@dataclass(frozen=True)
class CaptureResult:
    handle: int
    width: int
    height: int
    has_pixel_variation: bool
    appears_empty_or_white: bool
    transparent_multi: "TransparentMultiAnalysis"
    transparent_csg: "dict[str, TransparentCsgAnalysis]"
    texture_smoke: "TextureSmokeAnalysis"


@dataclass(frozen=True)
class ImageAnalysis:
    has_pixel_variation: bool
    appears_empty_or_white: bool


def capture_result_from_rgb_rows(handle, width, height, rows_rgb):
    analysis = analyze_rgb_rows(rows_rgb)
    transparent_multi = TransparentImageEvidence.analyze_transparent_multi_rows(rows_rgb)
    transparent_csg = {pose: TransparentImageEvidence.analyze_transparent_csg_rows(rows_rgb, pose) for pose in TRANSPARENT_CSG_REGIONS}
    texture_smoke = TextureSmokeImageEvidence.analyze_texture_smoke_rows(rows_rgb)
    return CaptureResult(handle, width, height, analysis.has_pixel_variation, analysis.appears_empty_or_white, transparent_multi, transparent_csg, texture_smoke,)


def write_capture_rows(handle, width, height, rows_rgb, output_path):
    write_bmp_24(output_path, width, height, rows_rgb)
    return capture_result_from_rgb_rows(handle, width, height, rows_rgb)


def read_bmp_24(path, handle=0):
    width, height, rows_rgb = read_bmp_24_rows(path)
    return capture_result_from_rgb_rows(handle, width, height, rows_rgb)


def analyze_rgb_rows(rows_rgb):
    first_pixel = None
    total_pixels = 0
    non_white_pixels = 0
    luma_sum = 0
    has_pixel_variation = False

    for row in rows_rgb:
        for red, green, blue in row:
            rgb = (red, green, blue)
            if first_pixel is None:
                first_pixel = rgb
            elif rgb != first_pixel:
                has_pixel_variation = True

            luma = (54 * red + 183 * green + 19 * blue) >> 8
            luma_sum += luma
            total_pixels += 1
            if red < 247 or green < 247 or blue < 247:
                non_white_pixels += 1

    if total_pixels == 0:
        return ImageAnalysis(False, True)

    mean_luma = luma_sum / total_pixels
    non_white_fraction = non_white_pixels / total_pixels
    appears_empty_or_white = mean_luma >= 245.0 and non_white_fraction < 0.01
    return ImageAnalysis(has_pixel_variation, appears_empty_or_white)


