"""Window-capture image IO and analysis: 24-bit BMP read/write, foreground and region pixel statistics, and the smoke-specific image oracles."""


import ctypes
from dataclasses import dataclass
import struct
from window_capture_outcome import SmokeSkip, WindowsDesktopUnavailable, SmokeFailure, write_status  # noqa: E402


LIT_B_BM = b"BM"
LIT_FRONT = "front"
LIT_SIDE = "side"
LIT_LITTLE = "little"
LIT_BIG = "big"
LIT_N_2SIHHI = "<2sIHHI"
# The textured-GI smoke has a fixed camera and a white receiver plane.
# This client-relative rectangle sits below the sphere silhouette, inside the nearby root of its foreground opaque direct shadow.
# Because the light is white and the receiver has no texture or tint, a red value here can only come from the textured sphere's indirect bounce.
TEXTURE_SMOKE_RECEIVER_REGION = (0.375, 0.667, 0.438, 0.700)
TEXTURE_SMOKE_RECEIVER_MIN_RED = 36
TEXTURE_SMOKE_RECEIVER_RED_CHROMA = 6
# CSG pose names match the fixture's frozen yaw values 0, 1, and 2 radians.
# Coordinates are offsets from image center, measured in image heights so the vertical-FOV projection remains valid at different aspect ratios.
# These inset rectangles come from the projected radius-.468 octahedron at camera distance2.2/FOV60 and its retained local-y<=0 pyramid, after Ry(yaw)*Ry(yaw)*Rx(.32*yaw)*Rz(.16*yaw).
# Each cut rectangle lies inside the uncut silhouette and outside the retained pyramid; each retained rectangle lies inside that pyramid.
# Geometry-based synthetic tests protect those margins.
# The late retained wedge is above/right of center because the cutter rotates with the receiver.
TRANSPARENT_CSG_REGIONS = { "early": ((-.030, -.125, .030, -.065), (-.030, .065, .030, .125)), "mid": ((.025, -.125, .065, -.080), (-.065, .110, -.025, .140)), "late": ((-.055, -.128, -.025, -.104), (.045, -.075, .080, -.050)), }


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


def write_bmp_24(path, width, height, rows_rgb):
    path.parent.mkdir(parents=True, exist_ok=True)

    row_stride = ((width * 3 + 3) // 4) * 4
    image_size = row_stride * height
    file_size = 14 + 40 + image_size
    padding = b"\0" * (row_stride - width * 3)

    with path.open("wb") as out:
        out.write(struct.pack(LIT_N_2SIHHI, LIT_B_BM, file_size, 0, 0, 14 + 40))
        out.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
        for row in reversed(rows_rgb):
            encoded = bytearray()
            for red, green, blue in row:
                encoded.extend((blue, green, red))
            out.write(encoded)
            out.write(padding)


def capture_result_from_rgb_rows(handle, width, height, rows_rgb):
    analysis = analyze_rgb_rows(rows_rgb)
    transparent_multi = analyze_transparent_multi_rows(rows_rgb)
    transparent_csg = {pose: analyze_transparent_csg_rows(rows_rgb, pose) for pose in TRANSPARENT_CSG_REGIONS}
    texture_smoke = analyze_texture_smoke_rows(rows_rgb)
    return CaptureResult(handle, width, height, analysis.has_pixel_variation, analysis.appears_empty_or_white, transparent_multi, transparent_csg, texture_smoke,)


def write_capture_rows(handle, width, height, rows_rgb, output_path):
    write_bmp_24(output_path, width, height, rows_rgb)
    return capture_result_from_rgb_rows(handle, width, height, rows_rgb)


def read_bmp_24(path, handle=0):
    width, height, rows_rgb = read_bmp_24_rows(path)
    return capture_result_from_rgb_rows(handle, width, height, rows_rgb)


def read_bmp_24_rows(path):
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SmokeFailure(f"failed to read application capture BMP '{path}': {error}") from error

    if len(data) < 54:
        raise SmokeFailure(f"application capture BMP '{path}' is shorter than the required headers")

    signature, declared_file_size, _, _, pixel_offset = struct.unpack_from(LIT_N_2SIHHI, data, 0)
    if signature != LIT_B_BM:
        raise SmokeFailure(f"application capture BMP '{path}' has an invalid signature")
    if declared_file_size != len(data):
        raise SmokeFailure(f"application capture BMP '{path}' declares {declared_file_size} bytes but contains {len(data)}")

    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40 or 14 + dib_size > len(data):
        raise SmokeFailure(f"application capture BMP '{path}' has an invalid DIB header size {dib_size}")

    width, signed_height, planes, bits_per_pixel, compression, image_size = struct.unpack_from("<iiHHII", data, 18)
    if width <= 0 or signed_height == 0:
        raise SmokeFailure(f"application capture BMP '{path}' has invalid dimensions {width}x{signed_height}")
    if planes != 1 or bits_per_pixel != 24 or compression != 0:
        raise SmokeFailure(f"application capture BMP '{path}' must be uncompressed 24-bit RGB " f"(planes={planes}, bits={bits_per_pixel}, compression={compression})")
    if pixel_offset < 14 + dib_size or pixel_offset > len(data):
        raise SmokeFailure(f"application capture BMP '{path}' has invalid pixel offset {pixel_offset}")

    height = abs(signed_height)
    row_stride = ((width * 3 + 3) // 4) * 4
    expected_image_size = row_stride * height
    expected_file_size = pixel_offset + expected_image_size
    if image_size not in (0, expected_image_size):
        raise SmokeFailure(f"application capture BMP '{path}' declares image size {image_size}, expected {expected_image_size}")
    if expected_file_size != len(data):
        raise SmokeFailure(f"application capture BMP '{path}' pixel payload ends at {expected_file_size}, file has {len(data)} bytes")

    stored_rows = []
    for stored_y in range(height):
        row_start = pixel_offset + stored_y * row_stride
        row = []
        for x in range(width):
            blue, green, red = data[row_start + x * 3:row_start + x * 3 + 3]
            row.append((red, green, blue))
        stored_rows.append(row)

    rows_rgb = stored_rows if signed_height < 0 else list(reversed(stored_rows))
    return width, height, rows_rgb


def validate_surfel_coverage_probe(path):
    """Check the interior of both same-cell, perpendicular faces in the opt-in GI scene."""
    width, height, rows = read_bmp_24_rows(path)
    # The cube is fixed in the 1280x900 smoke camera.
    # Fractional ROIs tolerate framebuffer scaling and avoid silhouette pixels, where normal/depth filtering should deliberately reduce coverage.
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


def analyze_transparent_multi_rows(rows_rgb):
    height = len(rows_rgb)
    width = len(rows_rgb[0]) if height else 0
    if width == 0 or height == 0:
        return TransparentMultiAnalysis(0, 0, 0)

    background = estimate_background_rgb(rows_rgb, width, height)

    left_pixels = count_foreground_pixels(rows_rgb, width, height, background, 0.16, 0.30, 0.43, 0.70)
    center_pixels = count_foreground_pixels(rows_rgb, width, height, background, 0.34, 0.27, 0.58, 0.70)
    right_nominal_pixels = count_foreground_pixels(rows_rgb, width, height, background, 0.62, 0.30, 0.90, 0.70)
    # The CSG smoke samples a late rotating pose where the right transparent mesh crosses the center/right boundary.
    right_rotated_pixels = count_foreground_pixels(rows_rgb, width, height, background, 0.47, 0.28, 0.66, 0.64)
    right_pixels = max(right_nominal_pixels, right_rotated_pixels)
    return TransparentMultiAnalysis(left_pixels, center_pixels, right_pixels)


def transparent_csg_regions(width, height, pose):
    def to_pixels(region):
        x0, y0, x1, y1 = region
        return normalized_region(width, height, .5 + x0 * height / width, .5 + y0, .5 + x1 * height / width, .5 + y1,)

    return tuple(to_pixels(region) for region in TRANSPARENT_CSG_REGIONS[pose])


def analyze_transparent_csg_rows(rows_rgb, pose):
    height = len(rows_rgb)
    width = len(rows_rgb[0]) if height else 0
    if width == 0 or height == 0:
        return TransparentCsgAnalysis(0, 0, 0, 0)

    background = estimate_background_rgb(rows_rgb, width, height)
    cut_region, remaining_region = transparent_csg_regions(width, height, pose)
    # The center's unlit green material is distinct from the red cylinder, blue cone, and gray opaque occluders.
    # Retain the ordinary foreground contrast threshold as well: color identity alone must not accept faint noise.
    remaining_center_pixels = count_foreground_pixels_in_region(rows_rgb, background, remaining_region, pixel_predicate=lambda rgb: rgb[1] > rgb[0] and rgb[1] > rgb[2],)
    return TransparentCsgAnalysis(count_background_like_pixels(rows_rgb, background, cut_region), region_pixel_count(cut_region), remaining_center_pixels, region_pixel_count(remaining_region),)


def analyze_texture_smoke_rows(rows_rgb):
    red_pixels = 0
    green_pixels = 0
    blue_pixels = 0
    height = len(rows_rgb)
    width = len(rows_rgb[0]) if height else 0
    receiver_region = normalized_region(width, height, *TEXTURE_SMOKE_RECEIVER_REGION)
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

    return TextureSmokeAnalysis(red_pixels, green_pixels, blue_pixels, receiver_red_pixels, region_pixel_count(receiver_region),)


def estimate_background_rgb(rows_rgb, width, height):
    samples = []
    edge = min(width, height, max(min(width, height) // 24, 8), 32)
    step = max(min(width, height) // 128, 1)

    for y in range(0, edge, step):
        samples.extend(rows_rgb[y][::step])
    for y in range(max(height - edge, 0), height, step):
        samples.extend(rows_rgb[y][::step])
    for row in rows_rgb[::step]:
        samples.extend(row[:edge:step])
        samples.extend(row[max(width - edge, 0):width:step])

    if not samples:
        return (0, 0, 0)

    return tuple(sorted(channel)[len(samples) // 2] for channel in zip(*samples))


def normalized_region(width, height, x0, y0, x1, y1):
    start_x = min(max(int(width * x0), 0), width)
    end_x = min(max(int(width * x1), start_x), width)
    start_y = min(max(int(height * y0), 0), height)
    end_y = min(max(int(height * y1), start_y), height)
    return start_x, start_y, end_x, end_y


def region_pixel_count(region):
    start_x, start_y, end_x, end_y = region
    return max(end_x - start_x, 0) * max(end_y - start_y, 0)


def count_foreground_pixels(rows_rgb, width, height, background, x0, y0, x1, y1):
    region = normalized_region(width, height, x0, y0, x1, y1)
    return count_foreground_pixels_in_region(rows_rgb, background, region)


def count_foreground_pixels_in_region(rows_rgb, background, region, pixel_predicate=None):
    start_x, start_y, end_x, end_y = region
    count = 0

    for y in range(start_y, end_y):
        for x in range(start_x, end_x):
            red, green, blue = rows_rgb[y][x]
            channel_delta = (abs(red - background[0]), abs(green - background[1]), abs(blue - background[2]),)
            if sum(channel_delta) >= 34 and max(channel_delta) >= 14:
                if pixel_predicate is None or pixel_predicate((red, green, blue)):
                    count += 1

    return count


def count_background_like_pixels(rows_rgb, background, region):
    start_x, start_y, end_x, end_y = region
    count = 0

    for y in range(start_y, end_y):
        for x in range(start_x, end_x):
            red, green, blue = rows_rgb[y][x]
            channel_delta = (abs(red - background[0]), abs(green - background[1]), abs(blue - background[2]),)
            if sum(channel_delta) <= 28 and max(channel_delta) <= 12:
                count += 1

    return count


def channel_from_mask(pixel, mask):
    if mask == 0:
        return 0

    low_bit = mask & -mask
    shift = low_bit.bit_length() - 1
    bit_count = mask.bit_length() - shift
    value = (pixel & mask) >> shift
    maximum = (1 << bit_count) - 1
    return (value * 255 + maximum // 2) // maximum


def ximage_rgb_rows(image, byte_order):
    bytes_per_pixel = (image.bits_per_pixel + 7) // 8
    raw_size = image.bytes_per_line * image.height
    raw = ctypes.string_at(image.data, raw_size)
    rows = []

    # Common RGB888 masks map directly to byte channels; keep scaled-mask conversion for other XImage layouts.
    if (image.bits_per_pixel in (24, 32) and (image.red_mask, image.green_mask, image.blue_mask) == (0xFF0000, 0xFF00, 0xFF)
            and byte_order in (LIT_LITTLE, LIT_BIG) and image.width >= 0 and image.bytes_per_line >= image.width * bytes_per_pixel):
        red, green, blue = (2, 1, 0) if byte_order == LIT_LITTLE else (bytes_per_pixel - 3, bytes_per_pixel - 2, bytes_per_pixel - 1)
        for y in range(image.height):
            row_base = y * image.bytes_per_line
            row_end = row_base + image.width * bytes_per_pixel
            rows.append(list(zip(raw[row_base + red:row_end:bytes_per_pixel], raw[row_base + green:row_end:bytes_per_pixel], raw[row_base + blue:row_end:bytes_per_pixel])))
        return rows

    for y in range(image.height):
        row_base = y * image.bytes_per_line
        row = []
        for x in range(image.width):
            pixel_base = row_base + x * bytes_per_pixel
            pixel = int.from_bytes(raw[pixel_base:pixel_base + bytes_per_pixel], byte_order)
            red = channel_from_mask(pixel, image.red_mask)
            green = channel_from_mask(pixel, image.green_mask)
            blue = channel_from_mask(pixel, image.blue_mask)
            row.append((red, green, blue))
        rows.append(row)

    return rows


def bgra_rgb_rows(buffer, width, height):
    rows = []
    for y in range(height):
        row = []
        row_base = y * width * 4
        for x in range(width):
            pixel_base = row_base + x * 4
            blue = buffer[pixel_base]
            green = buffer[pixel_base + 1]
            red = buffer[pixel_base + 2]
            row.append((red, green, blue))
        rows.append(row)

    return rows
