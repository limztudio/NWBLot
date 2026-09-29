#!/usr/bin/env python3

import argparse
import math
import pathlib
import re
import struct
import subprocess
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "common"))

from png_fixture import write_png_rows  # noqa: E402

# Shared literals (no inline hardcodes below this block).
LIT_ASCII = "ascii"
LIT_X00 = b"\x00"
LIT_COMPRESSION = "compression"
LIT_BOX2I = "box2i"
LIT_N_4I = "<4i"
LIT_LINEORDER = "lineOrder"
LIT_FLOAT = "float"
LIT_F = "<f"
LIT_SIZE_BYTES = "size_bytes"
LIT_LINEAR = "linear"
LIT_ASSET_BLOCK_WIDTH_4 = "asset.block_width = 4;"
LIT_ASSET_BLOCK_HEIGHT_4 = "asset.block_height = 4;"
LIT_ASSET_BYTES_PER_BLOCK_16 = "asset.bytes_per_block = 16;"
LIT_MIP_MAJOR_SLICE_MAJOR_BLOCKS = "mip_major_slice_major_blocks"
LIT_CLAMP = "clamp"
LIT_CHECKER_TEX = "checker.tex"
LIT_CUBE_TEX = "cube.tex"
LIT_VOLUME_TEX = "volume.tex"
LIT_RADIANCE_TEX = "radiance.tex"
LIT_OPENEXR_TEX = "openexr.tex"
LIT_HDR_CUBE_TEX = "hdr_cube.tex"
LIT_HDR_VOLUME_TEX = "hdr_volume.tex"
LIT_ORIGINAL = "original"
LIT_ORIGINAL_MASK = "original_mask"
LIT_MASK = "mask"
LIT_MASK_REFERENCE = "mask_reference"
LIT_WHITE = "white"
LIT_WHITE_REFERENCE = "white_reference"
LIT_BLACK = "black"
LIT_BLACK_REFERENCE = "black_reference"
LIT_NO_ALPHA = "no_alpha"
LIT_NWB = ".nwb"
LIT_TEX = ".tex"
LIT_NWB_TMP_SUFFIX = ".nwb.tmp"
LIT_TEX_TMP_SUFFIX = ".tex.tmp"
LIT_TMP_SUFFIX = ".tmp"
LIT_UTF_8 = "utf-8"
LIT_ASSET_VERSION_1 = "asset.version = 1;"
LIT_UASTC_LDR_4X4 = "uastc_ldr_4x4"
LIT_B624C07AD3C659E7B0F0BADCB36E9A6B8820A9 = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d"
LIT_N_2D = "2d"
LIT_ASSET_DEPTH_1 = "asset.depth = 1;"
LIT_ASSET_HAS_ALPHA_1 = "asset.has_alpha = 1;"
LIT_ASSET_MIP_COUNT_3 = "asset.mip_count = 3;"
LIT_LEVEL = "level"
LIT_WIDTH = "width"
LIT_HEIGHT = "height"
LIT_BLOCKS_X = "blocks_x"
LIT_BLOCKS_Y = "blocks_y"
LIT_OFFSET_BYTES = "offset_bytes"
LIT_SLICES = "slices"
LIT_OUTPUT = "--output"
LIT_LINEAR_2 = "--linear"
LIT_ALPHA = "--alpha"
LIT_ASSET_HAS_ALPHA_0 = "asset.has_alpha = 0;"
LIT_ASSET_WIDTH_4 = "asset.width = 4;"
LIT_ASSET_HEIGHT_2 = "asset.height = 2;"
LIT_OPAQUE = "opaque"
LIT_ASSET_ALPHA_CONSTANT_UNORM8 = "asset.alpha_constant_unorm8"
LIT_ASSET_ALPHA_PAYLOAD_OFFSET_BYTES = "asset.alpha_payload_offset_bytes"
LIT_ASSET_ALPHA_PAYLOAD_BYTE_COUNT = "asset.alpha_payload_byte_count"
LIT_ASSET_ALPHA_UASTC_SPEC_REVISION = "asset.alpha_uastc_spec_revision"
LIT_CUBE = "--cube"
LIT_CUBE_2 = "cube"
LIT_ASSET_WIDTH_2 = "asset.width = 2;"
LIT_ASSET_MIP_COUNT_2 = "asset.mip_count = 2;"
LIT_VOLUME = "--volume"
LIT_VOLUME_2 = "volume"
LIT_ASSET_DEPTH_3 = "asset.depth = 3;"
LIT_MAIN = "__main__"


def write_checker_png(path: pathlib.Path) -> None:
    width = 7
    height = 5
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(
                (
                    (x * 37 + y * 11) & 0xFF,
                    (x * 17 + y * 53) & 0xFF,
                    (x * 71 + y * 29) & 0xFF,
                    255 if (x + y) % 3 else 128,
                )
            )

    write_png_rows(path, width, height, 6, rows)


def write_rgba_png(path: pathlib.Path, width: int, height: int, color: tuple[int, int, int, int]) -> None:
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(
                (
                    (color[0] + x * 7 + y * 13) & 0xFF,
                    (color[1] + x * 11 + y * 5) & 0xFF,
                    (color[2] + x * 3 + y * 17) & 0xFF,
                    color[3],
                )
            )

    write_png_rows(path, width, height, 6, rows)


def write_png_pixels(
    path: pathlib.Path, width: int, height: int, color_type: int, pixels: tuple[tuple[int, ...], ...]
) -> None:
    channel_count = 4 if color_type == 6 else 3 if color_type == 2 else 0
    if channel_count == 0:
        raise AssertionError(f"unsupported PNG fixture color type: {color_type}")
    if len(pixels) != width * height:
        raise AssertionError("PNG fixture dimensions do not match the supplied pixel count")

    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for pixel in pixels[y * width : (y + 1) * width]:
            if len(pixel) != channel_count or any(component < 0 or component > 255 for component in pixel):
                raise AssertionError("PNG fixture has an invalid pixel")
            rows.extend(pixel)

    write_png_rows(path, width, height, color_type, rows)


def write_rgb_pixels_png(path: pathlib.Path, width: int, height: int, pixels: tuple[tuple[int, int, int], ...]) -> None:
    write_png_pixels(path, width, height, 2, pixels)


def write_rgba_pixels_png(
    path: pathlib.Path, width: int, height: int, pixels: tuple[tuple[int, int, int, int], ...]
) -> None:
    write_png_pixels(path, width, height, 6, pixels)


def require_matching_payloads(actual_path: pathlib.Path, expected_path: pathlib.Path, description: str) -> None:
    if actual_path.read_bytes() != expected_path.read_bytes():
        raise AssertionError(
            f"{description} produced a different UASTC payload than the equivalent RGBA reference "
            f"({actual_path} != {expected_path})"
        )


def float_to_rgbe(red: float, green: float, blue: float) -> bytes:
    maximum = max(red, green, blue)
    if maximum < 1.0e-32:
        return b"\x00\x00\x00\x00"

    mantissa, exponent = math.frexp(maximum)
    scale = mantissa * 256.0 / maximum
    return bytes(
        (
            max(0, min(255, int(red * scale))),
            max(0, min(255, int(green * scale))),
            max(0, min(255, int(blue * scale))),
            exponent + 128,
        )
    )


def write_radiance_hdr_pixels(
    path: pathlib.Path, width: int, height: int, pixels: tuple[tuple[float, float, float, float], ...]
) -> None:
    if len(pixels) != width * height:
        raise AssertionError("Radiance fixture dimensions do not match the supplied pixel count")
    payload = bytearray(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode(LIT_ASCII))
    for red, green, blue, _alpha in pixels:
        payload.extend(float_to_rgbe(red, green, blue))
    path.write_bytes(payload)


def write_radiance_hdr(path: pathlib.Path) -> None:
    width = 4
    height = 2
    first_pixel = (4.5, 0.25, 1.5, 1.0)
    pixels = (
        first_pixel,
        (0.5, 2.25, 7.0, 1.0),
        (1.0, 1.0, 1.0, 1.0),
        (0.125, 0.75, 3.5, 1.0),
        (8.0, 0.5, 0.25, 1.0),
        (0.25, 5.0, 0.5, 1.0),
        (2.0, 3.0, 4.0, 1.0),
        (0.5, 0.25, 0.125, 1.0),
    )
    write_radiance_hdr_pixels(path, width, height, pixels)


def exr_attribute(name: str, kind: str, value: bytes) -> bytes:
    return name.encode(LIT_ASCII) + LIT_X00 + kind.encode(LIT_ASCII) + LIT_X00 + struct.pack("<I", len(value)) + value


def write_openexr(path: pathlib.Path) -> None:
    # This is a minimal uncompressed scanline EXR writer for a float RGBA image.
    # Keeping it here makes the converter test independent of host image packages.
    width = 4
    height = 2
    first_pixel = (9.0, 0.5, 2.0, 0.375)
    pixels = (
        first_pixel,
        (0.25, 4.0, 0.75, 1.0),
        (1.25, 2.5, 6.0, 0.5),
        (0.125, 0.25, 0.5, 1.0),
        (3.0, 1.0, 0.5, 0.75),
        (0.5, 7.5, 0.25, 1.0),
        (2.0, 3.0, 4.0, 0.25),
        (0.75, 0.5, 0.25, 1.0),
    )
    channels = bytearray()
    for channel in ("A", "B", "G", "R"):
        channels.extend(channel.encode(LIT_ASCII) + LIT_X00)
        channels.extend(struct.pack("<iB3xii", 2, 0, 1, 1))
    channels.append(0)

    header = bytearray()
    header.extend(exr_attribute("channels", "chlist", bytes(channels)))
    header.extend(exr_attribute(LIT_COMPRESSION, LIT_COMPRESSION, LIT_X00))
    header.extend(exr_attribute("dataWindow", LIT_BOX2I, struct.pack(LIT_N_4I, 0, 0, width - 1, height - 1)))
    header.extend(exr_attribute("displayWindow", LIT_BOX2I, struct.pack(LIT_N_4I, 0, 0, width - 1, height - 1)))
    header.extend(exr_attribute(LIT_LINEORDER, LIT_LINEORDER, LIT_X00))
    header.extend(exr_attribute("pixelAspectRatio", LIT_FLOAT, struct.pack(LIT_F, 1.0)))
    header.extend(exr_attribute("screenWindowCenter", "v2f", struct.pack("<2f", 0.0, 0.0)))
    header.extend(exr_attribute("screenWindowWidth", LIT_FLOAT, struct.pack(LIT_F, 1.0)))
    header.append(0)

    chunk_data_size = width * 4 * 4
    first_chunk_offset = 8 + len(header) + height * 8
    output = bytearray(struct.pack("<II", 20000630, 2))
    output.extend(header)
    for row in range(height):
        output.extend(struct.pack("<Q", first_chunk_offset + row * (8 + chunk_data_size)))
    for row in range(height):
        output.extend(struct.pack("<ii", row, chunk_data_size))
        row_pixels = pixels[row * width : (row + 1) * width]
        for component in (3, 2, 1, 0):
            output.extend(struct.pack(f"<{width}f", *(pixel[component] for pixel in row_pixels)))
    path.write_bytes(output)


def require_metadata_fragment(metadata: str, fragment: str) -> None:
    if fragment not in metadata:
        raise AssertionError(f"metadata does not contain: {fragment!r}\n{metadata}")


def require_metadata_string_field(metadata: str, field: str) -> str:
    match = re.search(rf'^{re.escape(field)} = "([^"]+)";$', metadata, flags=re.MULTILINE)
    if match is None:
        raise AssertionError(f"metadata does not contain a nonempty string field {field!r}\n{metadata}")
    return match.group(1)


def require_metadata_unsigned_field(metadata: str, field: str, expected: int) -> None:
    require_metadata_fragment(metadata, f"{field} = {expected};")


def require_metadata_field_absent(metadata: str, field: str) -> None:
    if f"{field} =" in metadata:
        raise AssertionError(f"metadata unexpectedly contains {field!r}\n{metadata}")


def metadata_mip_payload_byte_count(metadata: str) -> int:
    mip_sizes = [int(size) for size in re.findall(r'"' + LIT_SIZE_BYTES + r'": (\d+)', metadata)]
    if not mip_sizes:
        raise AssertionError(f"metadata does not contain mip byte sizes\n{metadata}")
    return sum(mip_sizes)


def require_texture_payload_matches_metadata(metadata: str, texture_path: pathlib.Path) -> int:
    rgb_payload_bytes = metadata_mip_payload_byte_count(metadata)
    alpha_payload_match = re.search(r"^asset\.alpha_payload_byte_count = (\d+);$", metadata, flags=re.MULTILINE)
    alpha_payload_bytes = int(alpha_payload_match.group(1)) if alpha_payload_match else 0
    expected_payload_bytes = rgb_payload_bytes + alpha_payload_bytes
    actual_payload_bytes = texture_path.stat().st_size
    if actual_payload_bytes != expected_payload_bytes:
        raise AssertionError(
            f"texture payload size expected {expected_payload_bytes} bytes from metadata, got {actual_payload_bytes}: {texture_path}"
        )
    return rgb_payload_bytes


def require_uastc_hdr_metadata(metadata: str) -> None:
    for fragment in (
        "asset.version = 2;",
        'asset.format = "uastc_hdr_4x4";',
        'asset.color_space = "linear";',
        LIT_ASSET_BLOCK_WIDTH_4,
        LIT_ASSET_BLOCK_HEIGHT_4,
        LIT_ASSET_BYTES_PER_BLOCK_16,
        'asset.payload_layout = "mip_major_slice_major_blocks";',
        'asset.mip_address_mode = "clamp";',
    ):
        require_metadata_fragment(metadata, fragment)
    require_metadata_string_field(metadata, "asset.uastc_hdr_spec_revision")
    require_metadata_field_absent(metadata, "asset.uastc_spec_revision")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tex-conv", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    output_dir = pathlib.Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    source_path = output_dir / "checker.png"
    metadata_path = output_dir / "checker.nwb"
    texture_path = output_dir / LIT_CHECKER_TEX
    linear_metadata_path = output_dir / "linear.nwb"
    linear_texture_path = output_dir / "linear.tex"
    cube_metadata_path = output_dir / "cube.nwb"
    cube_texture_path = output_dir / LIT_CUBE_TEX
    volume_metadata_path = output_dir / "volume.nwb"
    volume_texture_path = output_dir / LIT_VOLUME_TEX
    radiance_source_path = output_dir / "radiance.hdr"
    radiance_metadata_path = output_dir / "radiance.nwb"
    radiance_texture_path = output_dir / LIT_RADIANCE_TEX
    radiance_white_metadata_path = output_dir / "radiance_white.nwb"
    radiance_white_texture_path = output_dir / "radiance_white.tex"
    radiance_black_metadata_path = output_dir / "radiance_black.nwb"
    radiance_black_texture_path = output_dir / "radiance_black.tex"
    openexr_source_path = output_dir / "openexr.exr"
    openexr_metadata_path = output_dir / "openexr.nwb"
    openexr_texture_path = output_dir / LIT_OPENEXR_TEX
    hdr_cube_metadata_path = output_dir / "hdr_cube.nwb"
    hdr_cube_texture_path = output_dir / LIT_HDR_CUBE_TEX
    hdr_volume_metadata_path = output_dir / "hdr_volume.nwb"
    hdr_volume_texture_path = output_dir / LIT_HDR_VOLUME_TEX
    alpha_source_path = output_dir / "alpha_source.png"
    alpha_mask_path = output_dir / "alpha_mask.png"
    alpha_original_mask_path = output_dir / "alpha_original_mask.png"
    alpha_mask_reference_path = output_dir / "alpha_mask_reference.png"
    alpha_white_reference_path = output_dir / "alpha_white_reference.png"
    alpha_black_reference_path = output_dir / "alpha_black_reference.png"
    alpha_no_alpha_source_path = output_dir / "alpha_no_alpha_source.png"
    alpha_output_bases = {
        LIT_ORIGINAL: output_dir / "alpha_original",
        LIT_ORIGINAL_MASK: output_dir / "alpha_original_mask",
        LIT_MASK: output_dir / "alpha_mask",
        LIT_MASK_REFERENCE: output_dir / "alpha_mask_reference",
        LIT_WHITE: output_dir / "alpha_white",
        LIT_WHITE_REFERENCE: output_dir / "alpha_white_reference",
        LIT_BLACK: output_dir / "alpha_black",
        LIT_BLACK_REFERENCE: output_dir / "alpha_black_reference",
        LIT_NO_ALPHA: output_dir / "alpha_no_alpha",
    }
    alpha_input_paths = (
        alpha_source_path,
        alpha_mask_path,
        alpha_original_mask_path,
        alpha_mask_reference_path,
        alpha_white_reference_path,
        alpha_black_reference_path,
        alpha_no_alpha_source_path,
    )
    alpha_output_paths = tuple(
        path
        for base in alpha_output_bases.values()
        for path in (
            base.with_suffix(LIT_NWB),
            base.with_suffix(LIT_TEX),
            pathlib.Path(f"{base}" + LIT_NWB_TMP_SUFFIX),
            pathlib.Path(f"{base}" + LIT_TEX_TMP_SUFFIX),
        )
    )
    cube_face_paths = [output_dir / f"cube_face_{face}.png" for face in range(6)]
    volume_slice_paths = [output_dir / f"volume_slice_{slice_index}.png" for slice_index in range(3)]
    hdr_cube_face_paths = [output_dir / f"hdr_cube_face_{face}.hdr" for face in range(6)]
    hdr_volume_slice_paths = [output_dir / f"hdr_volume_slice_{slice_index}.hdr" for slice_index in range(3)]
    for path in (
        source_path,
        metadata_path,
        texture_path,
        linear_metadata_path,
        linear_texture_path,
        cube_metadata_path,
        cube_texture_path,
        volume_metadata_path,
        volume_texture_path,
        radiance_source_path,
        radiance_metadata_path,
        radiance_texture_path,
        radiance_white_metadata_path,
        radiance_white_texture_path,
        radiance_black_metadata_path,
        radiance_black_texture_path,
        openexr_source_path,
        openexr_metadata_path,
        openexr_texture_path,
        hdr_cube_metadata_path,
        hdr_cube_texture_path,
        hdr_volume_metadata_path,
        hdr_volume_texture_path,
        *alpha_input_paths,
        *alpha_output_paths,
        pathlib.Path(str(metadata_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(texture_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(linear_metadata_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(linear_texture_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(cube_metadata_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(cube_texture_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(volume_metadata_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(volume_texture_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(radiance_metadata_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(radiance_texture_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(radiance_white_metadata_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(radiance_white_texture_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(radiance_black_metadata_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(radiance_black_texture_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(openexr_metadata_path) + LIT_TMP_SUFFIX),
        pathlib.Path(str(openexr_texture_path) + LIT_TMP_SUFFIX),
        *cube_face_paths,
        *volume_slice_paths,
        *hdr_cube_face_paths,
        *hdr_volume_slice_paths,
    ):
        path.unlink(missing_ok=True)

    write_checker_png(source_path)
    result = subprocess.run(
        [args.tex_conv, str(source_path)],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"tex_conv failed with {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if not metadata_path.is_file() or not texture_path.is_file():
        raise AssertionError(f"tex_conv did not create {metadata_path} and {texture_path}")

    metadata = metadata_path.read_text(encoding=LIT_UTF_8)
    for fragment in (
        "texture asset;",
        LIT_ASSET_VERSION_1,
        'asset.format = "uastc_ldr_4x4";',
        'asset.uastc_spec_revision = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d";',
        'asset.color_space = "srgb";',
        'asset.dimension = "2d";',
        LIT_ASSET_DEPTH_1,
        "asset.width = 7;",
        "asset.height = 5;",
        LIT_ASSET_BLOCK_WIDTH_4,
        LIT_ASSET_BLOCK_HEIGHT_4,
        LIT_ASSET_BYTES_PER_BLOCK_16,
        'asset.payload_layout = "mip_major_slice_major_blocks";',
        'asset.mip_address_mode = "clamp";',
        LIT_ASSET_HAS_ALPHA_1,
        LIT_ASSET_MIP_COUNT_3,
        'asset.data = "checker.tex";',
        '{ "' + LIT_LEVEL + '": 0' + ', "' + LIT_WIDTH + '": 7' + ', "' + LIT_HEIGHT + '": 5' + ', "' + LIT_BLOCKS_X + '": 2' + ', "' + LIT_BLOCKS_Y + '": 2' + ', "' + LIT_OFFSET_BYTES + '": 0' + ', "' + LIT_SIZE_BYTES + '": 64' + ', "' + LIT_SLICES + '": 1' + ' }',
        '{ "' + LIT_LEVEL + '": 1' + ', "' + LIT_WIDTH + '": 3' + ', "' + LIT_HEIGHT + '": 2' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 64' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
        '{ "' + LIT_LEVEL + '": 2' + ', "' + LIT_WIDTH + '": 1' + ', "' + LIT_HEIGHT + '": 1' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 80' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
    ):
        require_metadata_fragment(metadata, fragment)

    if texture_path.stat().st_size != 96:
        raise AssertionError(f"expected 96 UASTC bytes, got {texture_path.stat().st_size}")

    overwrite = subprocess.run(
        [args.tex_conv, str(source_path)],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    overwrite_output = f"{overwrite.stdout}\n{overwrite.stderr}"
    if overwrite.returncode != 1 or "Output already exists:" not in overwrite_output:
        raise AssertionError("tex_conv did not protect an existing output without --force")
    if "[ERROR]:" not in overwrite_output:
        raise AssertionError("tex_conv did not classify an existing-output rejection as an error")

    forced = subprocess.run(
        [args.tex_conv, str(source_path), "--force"],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if forced.returncode != 0:
        raise AssertionError(
            f"tex_conv --force failed with {forced.returncode}\nstdout:\n{forced.stdout}\nstderr:\n{forced.stderr}"
        )

    linear = subprocess.run(
        [args.tex_conv, str(source_path), LIT_OUTPUT, str(output_dir / LIT_LINEAR), LIT_LINEAR_2],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if linear.returncode != 0:
        raise AssertionError(
            f"tex_conv --linear failed with {linear.returncode}\nstdout:\n{linear.stdout}\nstderr:\n{linear.stderr}"
        )
    if not linear_metadata_path.is_file() or not linear_texture_path.is_file():
        raise AssertionError("tex_conv --output did not create the requested pair")
    require_metadata_fragment(
        linear_metadata_path.read_text(encoding=LIT_UTF_8),
        'asset.color_space = "linear";',
    )

    # Keep the LDR alpha cases at one UASTC block per level. The expected payloads are
    # separately encoded RGBA inputs, so byte-for-byte equality checks the effective
    # alpha values rather than only the `has_alpha` metadata bit.
    alpha_width = 4
    alpha_height = 4
    alpha_rgb_pixels = tuple(
        (
            (19 + x * 43 + y * 7) & 0xFF,
            (71 + x * 11 + y * 59) & 0xFF,
            (137 + x * 29 + y * 17) & 0xFF,
        )
        for y in range(alpha_height)
        for x in range(alpha_width)
    )
    original_alpha_values = (255, 0, 71, 19, 168, 253, 3, 110, 47, 202, 88, 144, 30, 226, 62, 180)
    mask_red_values = (12, 46, 91, 153, 218, 33, 76, 129, 182, 241, 65, 112, 168, 205, 28, 187)
    alpha_source_pixels = tuple(
        (*color, alpha) for color, alpha in zip(alpha_rgb_pixels, original_alpha_values)
    )
    alpha_mask_reference_pixels = tuple(
        (*color, alpha) for color, alpha in zip(alpha_rgb_pixels, mask_red_values)
    )
    alpha_white_reference_pixels = tuple((*color, 255) for color in alpha_rgb_pixels)
    alpha_black_reference_pixels = tuple((*color, 0) for color in alpha_rgb_pixels)

    def make_alpha_mask(red_values: tuple[int, ...]) -> tuple[tuple[int, int, int, int], ...]:
        # The remaining components deliberately disagree with red. In particular,
        # alpha is the inverse of red, proving that the mask's alpha channel is ignored.
        return tuple(
            (red, (red + 83) & 0xFF, (red + 159) & 0xFF, 255 - red) for red in red_values
        )

    write_rgba_pixels_png(alpha_source_path, alpha_width, alpha_height, alpha_source_pixels)
    write_rgba_pixels_png(
        alpha_original_mask_path, alpha_width, alpha_height, make_alpha_mask(original_alpha_values)
    )
    write_rgba_pixels_png(alpha_mask_path, alpha_width, alpha_height, make_alpha_mask(mask_red_values))
    write_rgba_pixels_png(alpha_mask_reference_path, alpha_width, alpha_height, alpha_mask_reference_pixels)
    write_rgba_pixels_png(alpha_white_reference_path, alpha_width, alpha_height, alpha_white_reference_pixels)
    write_rgba_pixels_png(alpha_black_reference_path, alpha_width, alpha_height, alpha_black_reference_pixels)
    write_rgb_pixels_png(alpha_no_alpha_source_path, alpha_width, alpha_height, alpha_rgb_pixels)

    def convert_alpha_fixture(name: str, source: pathlib.Path, *arguments: str) -> None:
        result = subprocess.run(
            [args.tex_conv, str(source), *arguments, LIT_OUTPUT, str(alpha_output_bases[name])],
            cwd=output_dir,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            raise AssertionError(
                f"tex_conv alpha fixture {name!r} failed with {result.returncode}"
                f"\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )

    def alpha_metadata(name: str) -> str:
        return alpha_output_bases[name].with_suffix(LIT_NWB).read_text(encoding=LIT_UTF_8)

    def alpha_texture(name: str) -> pathlib.Path:
        return alpha_output_bases[name].with_suffix(LIT_TEX)

    # With no --alpha flag, an input alpha channel is retained.
    convert_alpha_fixture(LIT_ORIGINAL, alpha_source_path)
    convert_alpha_fixture(LIT_ORIGINAL_MASK, alpha_source_path, LIT_ALPHA, str(alpha_original_mask_path))
    require_matching_payloads(
        alpha_texture(LIT_ORIGINAL), alpha_texture(LIT_ORIGINAL_MASK), "the original-alpha fallback"
    )
    require_metadata_fragment(alpha_metadata(LIT_ORIGINAL), LIT_ASSET_HAS_ALPHA_1)

    # A mask uses its red channel, not its own alpha, green, or blue channels.
    convert_alpha_fixture(LIT_MASK, alpha_source_path, LIT_ALPHA, str(alpha_mask_path))
    convert_alpha_fixture(LIT_MASK_REFERENCE, alpha_mask_reference_path)
    require_matching_payloads(alpha_texture(LIT_MASK), alpha_texture(LIT_MASK_REFERENCE), "the red-channel alpha mask")
    require_metadata_fragment(alpha_metadata(LIT_MASK), LIT_ASSET_HAS_ALPHA_1)

    convert_alpha_fixture(LIT_WHITE, alpha_source_path, LIT_ALPHA, LIT_WHITE)
    convert_alpha_fixture(LIT_WHITE_REFERENCE, alpha_white_reference_path)
    require_matching_payloads(alpha_texture(LIT_WHITE), alpha_texture(LIT_WHITE_REFERENCE), "--alpha white")
    require_metadata_fragment(alpha_metadata(LIT_WHITE), LIT_ASSET_HAS_ALPHA_0)

    convert_alpha_fixture(LIT_BLACK, alpha_source_path, LIT_ALPHA, LIT_BLACK)
    convert_alpha_fixture(LIT_BLACK_REFERENCE, alpha_black_reference_path)
    require_matching_payloads(alpha_texture(LIT_BLACK), alpha_texture(LIT_BLACK_REFERENCE), "--alpha black")
    require_metadata_fragment(alpha_metadata(LIT_BLACK), LIT_ASSET_HAS_ALPHA_1)

    # A source with no alpha channel falls back to opaque alpha when --alpha is omitted.
    convert_alpha_fixture(LIT_NO_ALPHA, alpha_no_alpha_source_path)
    require_matching_payloads(alpha_texture(LIT_NO_ALPHA), alpha_texture(LIT_WHITE_REFERENCE), "the no-alpha fallback")
    require_metadata_fragment(alpha_metadata(LIT_NO_ALPHA), LIT_ASSET_HAS_ALPHA_0)

    write_radiance_hdr(radiance_source_path)
    radiance = subprocess.run(
        [
            args.tex_conv,
            str(radiance_source_path),
            LIT_OUTPUT,
            str(output_dir / "radiance"),
            LIT_LINEAR_2,
        ],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if radiance.returncode != 0:
        raise AssertionError(
            f"tex_conv Radiance HDR failed with {radiance.returncode}\nstdout:\n{radiance.stdout}\nstderr:\n{radiance.stderr}"
    )
    radiance_metadata = radiance_metadata_path.read_text(encoding=LIT_UTF_8)
    require_uastc_hdr_metadata(radiance_metadata)
    for fragment in (
        'asset.dimension = "2d";',
        LIT_ASSET_DEPTH_1,
        LIT_ASSET_WIDTH_4,
        LIT_ASSET_HEIGHT_2,
        'asset.alpha_mode = "opaque";',
        LIT_ASSET_MIP_COUNT_3,
        'asset.data = "radiance.tex";',
        '{ "' + LIT_LEVEL + '": 0' + ', "' + LIT_WIDTH + '": 4' + ', "' + LIT_HEIGHT + '": 2' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 0' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
        '{ "' + LIT_LEVEL + '": 1' + ', "' + LIT_WIDTH + '": 2' + ', "' + LIT_HEIGHT + '": 1' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 16' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
        '{ "' + LIT_LEVEL + '": 2' + ', "' + LIT_WIDTH + '": 1' + ', "' + LIT_HEIGHT + '": 1' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 32' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
    ):
        require_metadata_fragment(radiance_metadata, fragment)
    for field in (
        LIT_ASSET_ALPHA_CONSTANT_UNORM8,
        LIT_ASSET_ALPHA_PAYLOAD_OFFSET_BYTES,
        LIT_ASSET_ALPHA_PAYLOAD_BYTE_COUNT,
        LIT_ASSET_ALPHA_UASTC_SPEC_REVISION,
    ):
        require_metadata_field_absent(radiance_metadata, field)
    require_texture_payload_matches_metadata(radiance_metadata, radiance_texture_path)

    radiance_white = subprocess.run(
        [args.tex_conv, str(radiance_source_path), LIT_ALPHA, LIT_WHITE, LIT_OUTPUT, str(output_dir / "radiance_white")],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if radiance_white.returncode != 0:
        raise AssertionError(
            f"tex_conv HDR --alpha white failed with {radiance_white.returncode}"
            f"\nstdout:\n{radiance_white.stdout}\nstderr:\n{radiance_white.stderr}"
        )
    radiance_white_metadata = radiance_white_metadata_path.read_text(encoding=LIT_UTF_8)
    require_uastc_hdr_metadata(radiance_white_metadata)
    require_metadata_fragment(radiance_white_metadata, LIT_ASSET_HAS_ALPHA_0)
    require_metadata_fragment(radiance_white_metadata, 'asset.alpha_mode = "opaque";')
    for field in (
        LIT_ASSET_ALPHA_CONSTANT_UNORM8,
        LIT_ASSET_ALPHA_PAYLOAD_OFFSET_BYTES,
        LIT_ASSET_ALPHA_PAYLOAD_BYTE_COUNT,
        LIT_ASSET_ALPHA_UASTC_SPEC_REVISION,
    ):
        require_metadata_field_absent(radiance_white_metadata, field)
    require_texture_payload_matches_metadata(radiance_white_metadata, radiance_white_texture_path)
    require_matching_payloads(radiance_white_texture_path, radiance_texture_path, "HDR --alpha white")

    radiance_black = subprocess.run(
        [args.tex_conv, str(radiance_source_path), LIT_ALPHA, LIT_BLACK, LIT_OUTPUT, str(output_dir / "radiance_black")],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if radiance_black.returncode != 0:
        raise AssertionError(
            f"tex_conv HDR --alpha black failed with {radiance_black.returncode}"
            f"\nstdout:\n{radiance_black.stdout}\nstderr:\n{radiance_black.stderr}"
        )
    radiance_black_metadata = radiance_black_metadata_path.read_text(encoding=LIT_UTF_8)
    require_uastc_hdr_metadata(radiance_black_metadata)
    require_metadata_fragment(radiance_black_metadata, LIT_ASSET_HAS_ALPHA_1)
    require_metadata_fragment(radiance_black_metadata, 'asset.alpha_mode = "constant_unorm8";')
    require_metadata_unsigned_field(radiance_black_metadata, LIT_ASSET_ALPHA_CONSTANT_UNORM8, 0)
    for field in (
        LIT_ASSET_ALPHA_PAYLOAD_OFFSET_BYTES,
        LIT_ASSET_ALPHA_PAYLOAD_BYTE_COUNT,
        LIT_ASSET_ALPHA_UASTC_SPEC_REVISION,
    ):
        require_metadata_field_absent(radiance_black_metadata, field)
    require_texture_payload_matches_metadata(radiance_black_metadata, radiance_black_texture_path)
    require_matching_payloads(radiance_black_texture_path, radiance_texture_path, "HDR --alpha black RGB stream")

    write_openexr(openexr_source_path)
    openexr = subprocess.run(
        [args.tex_conv, str(openexr_source_path), LIT_OUTPUT, str(output_dir / "openexr")],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if openexr.returncode != 0:
        raise AssertionError(
            f"tex_conv OpenEXR failed with {openexr.returncode}\nstdout:\n{openexr.stdout}\nstderr:\n{openexr.stderr}"
    )
    openexr_metadata = openexr_metadata_path.read_text(encoding=LIT_UTF_8)
    require_uastc_hdr_metadata(openexr_metadata)
    for fragment in (
        'asset.dimension = "2d";',
        LIT_ASSET_DEPTH_1,
        LIT_ASSET_WIDTH_4,
        LIT_ASSET_HEIGHT_2,
        'asset.alpha_mode = "uastc_ldr_4x4";',
        LIT_ASSET_MIP_COUNT_3,
        'asset.data = "openexr.tex";',
        '{ "' + LIT_LEVEL + '": 0' + ', "' + LIT_WIDTH + '": 4' + ', "' + LIT_HEIGHT + '": 2' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 0' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
        '{ "' + LIT_LEVEL + '": 1' + ', "' + LIT_WIDTH + '": 2' + ', "' + LIT_HEIGHT + '": 1' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 16' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
        '{ "' + LIT_LEVEL + '": 2' + ', "' + LIT_WIDTH + '": 1' + ', "' + LIT_HEIGHT + '": 1' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 32' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
    ):
        require_metadata_fragment(openexr_metadata, fragment)
    openexr_rgb_payload_bytes = metadata_mip_payload_byte_count(openexr_metadata)
    require_metadata_unsigned_field(
        openexr_metadata, LIT_ASSET_ALPHA_PAYLOAD_OFFSET_BYTES, openexr_rgb_payload_bytes
    )
    require_metadata_unsigned_field(
        openexr_metadata, LIT_ASSET_ALPHA_PAYLOAD_BYTE_COUNT, openexr_rgb_payload_bytes
    )
    require_metadata_fragment(
        openexr_metadata,
        'asset.alpha_uastc_spec_revision = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d";',
    )
    require_metadata_field_absent(openexr_metadata, LIT_ASSET_ALPHA_CONSTANT_UNORM8)
    require_texture_payload_matches_metadata(openexr_metadata, openexr_texture_path)

    for face_index, face_path in enumerate(hdr_cube_face_paths):
        write_radiance_hdr_pixels(
            face_path,
            2,
            2,
            (
                (2.0 + face_index, 0.25, 1.0, 1.0),
                (0.5, 3.0 + face_index, 0.25, 1.0),
                (0.25, 0.5, 4.0 + face_index, 1.0),
                (1.0, 1.0, 1.0, 1.0),
            ),
        )
    hdr_cube = subprocess.run(
        [
            args.tex_conv,
            LIT_CUBE,
            *(str(face_path) for face_path in hdr_cube_face_paths),
            LIT_OUTPUT,
            str(output_dir / "hdr_cube"),
        ],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if hdr_cube.returncode != 0:
        raise AssertionError(
            f"tex_conv HDR cube failed with {hdr_cube.returncode}\nstdout:\n{hdr_cube.stdout}\nstderr:\n{hdr_cube.stderr}"
    )
    hdr_cube_metadata = hdr_cube_metadata_path.read_text(encoding=LIT_UTF_8)
    require_uastc_hdr_metadata(hdr_cube_metadata)
    for fragment in (
        'asset.dimension = "cube";',
        LIT_ASSET_DEPTH_1,
        LIT_ASSET_WIDTH_2,
        LIT_ASSET_HEIGHT_2,
        'asset.alpha_mode = "opaque";',
        LIT_ASSET_MIP_COUNT_2,
        'asset.data = "hdr_cube.tex";',
        '{ "' + LIT_LEVEL + '": 0' + ', "' + LIT_WIDTH + '": 2' + ', "' + LIT_HEIGHT + '": 2' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 0' + ', "' + LIT_SIZE_BYTES + '": 96' + ', "' + LIT_SLICES + '": 6' + ' }',
        '{ "' + LIT_LEVEL + '": 1' + ', "' + LIT_WIDTH + '": 1' + ', "' + LIT_HEIGHT + '": 1' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 96' + ', "' + LIT_SIZE_BYTES + '": 96' + ', "' + LIT_SLICES + '": 6' + ' }',
    ):
        require_metadata_fragment(hdr_cube_metadata, fragment)
    for field in (
        LIT_ASSET_ALPHA_CONSTANT_UNORM8,
        LIT_ASSET_ALPHA_PAYLOAD_OFFSET_BYTES,
        LIT_ASSET_ALPHA_PAYLOAD_BYTE_COUNT,
        LIT_ASSET_ALPHA_UASTC_SPEC_REVISION,
    ):
        require_metadata_field_absent(hdr_cube_metadata, field)
    require_texture_payload_matches_metadata(hdr_cube_metadata, hdr_cube_texture_path)

    for slice_path in hdr_volume_slice_paths:
        write_radiance_hdr(slice_path)
    hdr_volume = subprocess.run(
        [
            args.tex_conv,
            LIT_VOLUME,
            *(str(slice_path) for slice_path in hdr_volume_slice_paths),
            LIT_OUTPUT,
            str(output_dir / "hdr_volume"),
        ],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if hdr_volume.returncode != 0:
        raise AssertionError(
            f"tex_conv HDR volume failed with {hdr_volume.returncode}\nstdout:\n{hdr_volume.stdout}\nstderr:\n{hdr_volume.stderr}"
    )
    hdr_volume_metadata = hdr_volume_metadata_path.read_text(encoding=LIT_UTF_8)
    require_uastc_hdr_metadata(hdr_volume_metadata)
    for fragment in (
        'asset.dimension = "volume";',
        LIT_ASSET_DEPTH_3,
        LIT_ASSET_WIDTH_4,
        LIT_ASSET_HEIGHT_2,
        'asset.alpha_mode = "opaque";',
        LIT_ASSET_MIP_COUNT_3,
        'asset.data = "hdr_volume.tex";',
        '{ "' + LIT_LEVEL + '": 0' + ', "' + LIT_WIDTH + '": 4' + ', "' + LIT_HEIGHT + '": 2' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 0' + ', "' + LIT_SIZE_BYTES + '": 48' + ', "' + LIT_SLICES + '": 3' + ' }',
        '{ "' + LIT_LEVEL + '": 1' + ', "' + LIT_WIDTH + '": 2' + ', "' + LIT_HEIGHT + '": 1' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 48' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
        '{ "' + LIT_LEVEL + '": 2' + ', "' + LIT_WIDTH + '": 1' + ', "' + LIT_HEIGHT + '": 1' + ', "' + LIT_BLOCKS_X + '": 1' + ', "' + LIT_BLOCKS_Y + '": 1' + ', "' + LIT_OFFSET_BYTES + '": 64' + ', "' + LIT_SIZE_BYTES + '": 16' + ', "' + LIT_SLICES + '": 1' + ' }',
    ):
        require_metadata_fragment(hdr_volume_metadata, fragment)
    for field in (
        LIT_ASSET_ALPHA_CONSTANT_UNORM8,
        LIT_ASSET_ALPHA_PAYLOAD_OFFSET_BYTES,
        LIT_ASSET_ALPHA_PAYLOAD_BYTE_COUNT,
        LIT_ASSET_ALPHA_UASTC_SPEC_REVISION,
    ):
        require_metadata_field_absent(hdr_volume_metadata, field)
    require_texture_payload_matches_metadata(hdr_volume_metadata, hdr_volume_texture_path)

    # Faces are supplied in the on-disk/Basis convention: +X, -X, +Y, -Y, +Z, -Z.
    for face_index, face_path in enumerate(cube_face_paths):
        write_rgba_png(face_path, 2, 2, (face_index * 31, face_index * 17, face_index * 47, 255))
    cube = subprocess.run(
        [
            args.tex_conv,
            LIT_CUBE,
            *(str(face_path) for face_path in cube_face_paths),
            LIT_OUTPUT,
            str(output_dir / LIT_CUBE_2),
        ],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if cube.returncode != 0:
        raise AssertionError(
            f"tex_conv --cube failed with {cube.returncode}\nstdout:\n{cube.stdout}\nstderr:\n{cube.stderr}"
        )
    if not cube_metadata_path.is_file() or not cube_texture_path.is_file():
        raise AssertionError("tex_conv --cube did not create the requested pair")
    cube_metadata = cube_metadata_path.read_text(encoding=LIT_UTF_8)
    for fragment in (
        LIT_ASSET_VERSION_1,
        'asset.dimension = "cube";',
        LIT_ASSET_DEPTH_1,
        LIT_ASSET_WIDTH_2,
        LIT_ASSET_HEIGHT_2,
        'asset.payload_layout = "mip_major_slice_major_blocks";',
        LIT_ASSET_MIP_COUNT_2,
        '"' + LIT_LEVEL + '": 0, "' + LIT_WIDTH + '": 2, "' + LIT_HEIGHT + '": 2',
        '"' + LIT_LEVEL + '": 1, "' + LIT_WIDTH + '": 1, "' + LIT_HEIGHT + '": 1',
        '"' + LIT_SLICES + '": 6',
        'asset.data = "cube.tex";',
    ):
        require_metadata_fragment(cube_metadata, fragment)
    if cube_texture_path.stat().st_size != 192:
        raise AssertionError(f"expected 192 cube UASTC bytes, got {cube_texture_path.stat().st_size}")

    # Volume inputs are ordered z=0 through z=depth-1. 4x2x3 exercises a depth-reduced complete 3D mip chain.
    for slice_index, slice_path in enumerate(volume_slice_paths):
        write_rgba_png(slice_path, 4, 2, (slice_index * 43, 80 + slice_index * 19, 170 - slice_index * 23, 255))
    volume = subprocess.run(
        [
            args.tex_conv,
            LIT_VOLUME,
            *(str(slice_path) for slice_path in volume_slice_paths),
            LIT_OUTPUT,
            str(output_dir / LIT_VOLUME_2),
        ],
        cwd=output_dir,
        text=True,
        capture_output=True,
    )
    if volume.returncode != 0:
        raise AssertionError(
            f"tex_conv --volume failed with {volume.returncode}\nstdout:\n{volume.stdout}\nstderr:\n{volume.stderr}"
        )
    if not volume_metadata_path.is_file() or not volume_texture_path.is_file():
        raise AssertionError("tex_conv --volume did not create the requested pair")
    volume_metadata = volume_metadata_path.read_text(encoding=LIT_UTF_8)
    for fragment in (
        LIT_ASSET_VERSION_1,
        'asset.dimension = "volume";',
        LIT_ASSET_DEPTH_3,
        LIT_ASSET_WIDTH_4,
        LIT_ASSET_HEIGHT_2,
        'asset.payload_layout = "mip_major_slice_major_blocks";',
        LIT_ASSET_MIP_COUNT_3,
        '"' + LIT_LEVEL + '": 0, "' + LIT_WIDTH + '": 4, "' + LIT_HEIGHT + '": 2',
        '"' + LIT_LEVEL + '": 1, "' + LIT_WIDTH + '": 2, "' + LIT_HEIGHT + '": 1',
        '"' + LIT_LEVEL + '": 2, "' + LIT_WIDTH + '": 1, "' + LIT_HEIGHT + '": 1',
        '"' + LIT_SLICES + '": 3',
        'asset.data = "volume.tex";',
    ):
        require_metadata_fragment(volume_metadata, fragment)
    if volume_texture_path.stat().st_size != 80:
        raise AssertionError(f"expected 80 volume UASTC bytes, got {volume_texture_path.stat().st_size}")
    return 0


if __name__ == LIT_MAIN:
    try:
        raise SystemExit(main())
    except Exception as exception:
        print(f"tex_conv test failed: {exception}", file=sys.stderr)
        raise SystemExit(1)
