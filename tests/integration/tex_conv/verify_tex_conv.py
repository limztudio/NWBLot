#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import math
import pathlib
import re
import struct
import subprocess
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "common"))

from png_fixture import write_png_rows  # noqa: E402

LIT_ASCII = "ascii"
LIT_X00 = b"\x00"
LIT_COMPRESSION = "compression"
LIT_BOX2I = "box2i"
LIT_N_4I = "<4i"
LIT_LINEORDER = "lineOrder"
LIT_FLOAT = "float"
LIT_F = "<f"
LIT_TMP_SUFFIX = ".tmp"
LIT_LINEAR = "--linear"
EXR_UINT = 0
EXR_FLOAT = 2


def write_png_pixels(path: pathlib.Path, width: int, height: int, color_type: int, pixels: tuple[tuple[int, ...], ...]) -> None:
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


def float_to_rgbe(red: float, green: float, blue: float) -> bytes:
    maximum = max(red, green, blue)
    if maximum < 1.0e-32:
        return b"\x00\x00\x00\x00"

    mantissa, exponent = math.frexp(maximum)
    scale = mantissa * 256.0 / maximum
    return bytes((max(0, min(255, int(red * scale))), max(0, min(255, int(green * scale))), max(0, min(255, int(blue * scale))), exponent + 128,))


def write_radiance_hdr_pixels(path: pathlib.Path, width: int, height: int, pixels: tuple[tuple[float, float, float, float], ...]) -> None:
    if len(pixels) != width * height:
        raise AssertionError("Radiance fixture dimensions do not match the supplied pixel count")
    payload = bytearray(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode(LIT_ASCII))
    for red, green, blue, _alpha in pixels:
        payload.extend(float_to_rgbe(red, green, blue))
    path.write_bytes(payload)


def exr_attribute(name: str, kind: str, value: bytes) -> bytes:
    return name.encode(LIT_ASCII) + LIT_X00 + kind.encode(LIT_ASCII) + LIT_X00 + struct.pack("<I", len(value)) + value


def write_openexr(path: pathlib.Path, pixels: tuple[tuple[float, float, float, float], ...], channel_names: tuple[str, ...] = ("A", "B", "G", "R"), channel_types: tuple[int, ...] = ()) -> None:
    # This is a minimal uncompressed scanline EXR writer with explicit UINT or FLOAT channel declarations.
    # Keeping it here makes the converter test independent of host image packages.
    width = 4
    height = 2
    if len(pixels) != width * height:
        raise AssertionError("EXR fixture dimensions do not match the supplied pixel count")
    if not channel_types:
        channel_types = (EXR_FLOAT,) * len(channel_names)
    if len(channel_types) != len(channel_names) or any(pixel_type not in (EXR_UINT, EXR_FLOAT) for pixel_type in channel_types):
        raise AssertionError("EXR fixture requires one supported pixel type per channel")
    channels = bytearray()
    for channel, pixel_type in zip(channel_names, channel_types):
        channels.extend(channel.encode(LIT_ASCII) + LIT_X00)
        channels.extend(struct.pack("<iB3xii", pixel_type, 0, 1, 1))
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

    chunk_data_size = width * len(channel_names) * 4
    first_chunk_offset = 8 + len(header) + height * 8
    output = bytearray(struct.pack("<II", 20000630, 2))
    output.extend(header)
    for row in range(height):
        output.extend(struct.pack("<Q", first_chunk_offset + row * (8 + chunk_data_size)))
    for row in range(height):
        output.extend(struct.pack("<ii", row, chunk_data_size))
        row_pixels = pixels[row * width : (row + 1) * width]
        for channel, pixel_type in zip(channel_names, channel_types):
            component = {"R": 0, "G": 1, "B": 2, "A": 3, "Y": 0}[channel]
            packing = "I" if pixel_type == EXR_UINT else "f"
            output.extend(struct.pack(f"<{width}{packing}", *(pixel[component] for pixel in row_pixels)))
    path.write_bytes(output)


def verify_publication_failure_preserves_outputs(executable: str, source_path: pathlib.Path, metadata_path: pathlib.Path, texture_path: pathlib.Path, output_dir: pathlib.Path,) -> None:
    outputs = (texture_path, metadata_path)
    originals = tuple(path.read_bytes() for path in outputs)
    backups = tuple(pathlib.Path(str(path) + ".old") for path in outputs)
    temporaries = tuple(pathlib.Path(str(path) + LIT_TMP_SUFFIX) for path in outputs)

    def require_preserved_pair() -> None:
        if tuple(path.read_bytes() for path in outputs) != originals:
            raise AssertionError("failed forced publication changed the previous texture pair")
        if any(path.exists() for path in temporaries):
            raise AssertionError("failed publication retained a temporary owned by the converter")

    for backup in backups:
        sentinel = b"unrelated recovery file"
        backup.write_bytes(sentinel)
        try:
            result = subprocess.run([executable, str(source_path), "--force", LIT_LINEAR], cwd=output_dir, text=True, capture_output=True,)
            if result.returncode != 1:
                raise AssertionError("tex_conv did not refuse an occupied publication backup path")
            if backup.read_bytes() != sentinel:
                raise AssertionError("tex_conv overwrote an existing recovery file")
            require_preserved_pair()
        finally:
            backup.unlink()

    if sys.platform.startswith("linux"):
        missing_target = output_dir / "absent_publication_backup_target"
        if missing_target.exists():
            raise AssertionError("dangling-link fixture target unexpectedly exists")
        backups[0].symlink_to(missing_target.name)
        try:
            result = subprocess.run([executable, str(source_path), "--force", LIT_LINEAR], cwd=output_dir, text=True, capture_output=True,)
            if result.returncode != 1 or not backups[0].is_symlink():
                raise AssertionError("tex_conv did not preserve an occupied dangling backup link")
            require_preserved_pair()
        finally:
            backups[0].unlink()

    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,)
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel32.CloseHandle.restype = wintypes.BOOL
        # Deny deletion of the second destination so the first backup must be restored.
        handle = kernel32.CreateFileW(str(metadata_path), 0x80000000, 0x1 | 0x2, None, 3, 0x80, None)
        if handle == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_last_error(), "could not lock publication fixture")
        try:
            result = subprocess.run([executable, str(source_path), "--force", LIT_LINEAR], cwd=output_dir, text=True, capture_output=True,)
        finally:
            if not kernel32.CloseHandle(handle):
                raise OSError(ctypes.get_last_error(), "could not release publication fixture")
        if result.returncode != 1 or "failed to preserve existing output" not in result.stdout + result.stderr:
            raise AssertionError("the sharing lock did not exercise second-output backup failure")
        require_preserved_pair()
        if any(path.exists() for path in backups):
            raise AssertionError("rollback did not restore the previous output locations")

    recovered = subprocess.run([executable, str(source_path), "--force"], cwd=output_dir, text=True, capture_output=True,)
    if recovered.returncode != 0:
        raise AssertionError("tex_conv could not publish after the obstruction was removed")
    if tuple(path.read_bytes() for path in outputs) != originals:
        raise AssertionError("publication after failure did not recover the original conversion")
    if any(path.exists() for path in (*backups, *temporaries)):
        raise AssertionError("successful recovery left publication work files")


def read_raw_pair(path: pathlib.Path) -> dict:
    metadata_path = path.with_suffix(".nwb")
    metadata = metadata_path.read_text(encoding="utf-8")
    assignments = re.findall(r"asset\.(\w+)\s*=\s*([^;]+);", metadata)
    fields = dict(assignments)
    required = {"format", "dimension", "resolution", "alpha_mode"}
    if len(fields) != len(assignments) or set(fields) not in (required, required | {"alpha_constant_unorm8"}):
        raise AssertionError(f"texture metadata contains missing, duplicate, or unsupported fields: {metadata_path}\n{metadata}")
    try:
        values = {name: json.loads(value) for name, value in fields.items()}
    except json.JSONDecodeError as error:
        raise AssertionError(f"texture metadata contains malformed descriptor values: {metadata_path}") from error
    formats = {"uastc_ldr_4x4": 0, "uastc_hdr_4x4": 1}
    dimensions = {"2d": 0, "cube": 1, "volume": 2}
    alpha_modes = {"opaque": 0, "embedded_ldr": 1, "constant_unorm8": 2, "separate_uastc_ldr_4x4": 3}
    if values["format"] not in formats or values["dimension"] not in dimensions or values["alpha_mode"] not in alpha_modes:
        raise AssertionError(f"texture metadata has an unsupported format, dimension, or alpha mode: {metadata_path}")
    dimension = dimensions[values["dimension"]]
    payload_format = formats[values["format"]]
    alpha_mode = alpha_modes[values["alpha_mode"]]
    resolution = values["resolution"]
    if not isinstance(resolution, list) or len(resolution) != (3 if dimension == 2 else 2) or any(type(value) is not int or value <= 0 for value in resolution):
        raise AssertionError(f"invalid texture resolution: {metadata_path}")
    width, height = resolution[:2]
    depth = resolution[2] if dimension == 2 else 1
    count = max(width, height, depth).bit_length()
    if dimension == 1 and width != height:
        raise AssertionError(f"cube texture faces must be square: {metadata_path}")
    if (alpha_mode == 2) != ("alpha_constant_unorm8" in values):
        raise AssertionError(f"alpha constant must appear only for constant transport: {metadata_path}")
    alpha_constant = values.get("alpha_constant_unorm8", 255)
    if alpha_mode == 2 and (type(alpha_constant) is not int or not 0 <= alpha_constant <= 254):
        raise AssertionError(f"invalid alpha constant: {metadata_path}")
    if (payload_format == 0 and alpha_mode not in (0, 1)) or (payload_format == 1 and alpha_mode == 1):
        raise AssertionError(f"incompatible alpha transport: {metadata_path}")
    records = []
    mip_width, mip_height, mip_depth, primary_size = width, height, depth, 0
    for level in range(count):
        planes = 6 if dimension == 1 else mip_depth if dimension == 2 else 1
        primary_size += ((mip_width + 3) // 4) * ((mip_height + 3) // 4) * 16 * planes
        records.append((mip_width, mip_height, planes))
        terminal = mip_width == mip_height == 1 and (dimension != 2 or mip_depth == 1)
        if terminal != (level == count - 1):
            raise AssertionError(f"texture mip chain is incomplete or excessive: {metadata_path}")
        mip_width, mip_height = max(1, mip_width // 2), max(1, mip_height // 2)
        mip_depth = max(1, mip_depth // 2) if dimension == 2 else 1
    if len(path.read_bytes()) != primary_size * (2 if alpha_mode == 3 else 1):
        raise AssertionError(f"raw sidecar length does not match metadata mip and alpha layout: {path}")
    return {"dimension": dimension, "format": payload_format, "alpha_mode": alpha_mode, "alpha_constant": alpha_constant, "mips": records}


def run_conversion(executable: str, output_dir: pathlib.Path, name: str, inputs: tuple[pathlib.Path, ...], *options: str) -> pathlib.Path:
    base = output_dir / name
    mode = tuple(option for option in options if option in ("--cube", "--volume"))
    remaining = tuple(option for option in options if option not in ("--cube", "--volume"))
    command = [executable, *mode, *(str(path) for path in inputs), *remaining, "--force", "--output", str(base)]
    result = subprocess.run(command, cwd=output_dir, text=True, capture_output=True)
    if result.returncode != 0:
        raise AssertionError(f"conversion failed: {command}\n{result.stdout}\n{result.stderr}")
    read_raw_pair(base.with_suffix(".tex"))
    return base.with_suffix(".tex")


def decode_pixels(executable: str, prepared: pathlib.Path, level: int = 0) -> tuple[tuple[float, ...], ...]:
    description = read_raw_pair(prepared)
    extent = description["mips"][level]
    destination = prepared.with_suffix(f".mip{level}.decoded")
    result = subprocess.run([executable, str(prepared.with_suffix(".nwb")), str(destination), str(level)], text=True, capture_output=True)
    if result.returncode != 0:
        raise AssertionError(f"native prepared texture decode failed: {prepared}, mip {level}\n{result.stdout}\n{result.stderr}")
    binary = destination.read_bytes()
    count = math.prod(extent)
    if len(binary) != 16 + count * 16 or struct.unpack_from("<4I", binary) != (*extent, 4):
        raise AssertionError(f"native decoded texture extent is invalid: {prepared}")
    return tuple(struct.unpack_from("<4f", binary, 16 + index * 16) for index in range(count))


def require_pixels(actual: tuple[tuple[float, ...], ...], expected: tuple[tuple[float, ...], ...], absolute: float = 2.0 / 255.0, relative: float = 0.0) -> None:
    if len(actual) != len(expected):
        raise AssertionError(f"pixel count differs: {len(actual)} != {len(expected)}")
    for index, (observed, reference) in enumerate(zip(actual, expected)):
        for channel, (value, target) in enumerate(zip(observed, reference)):
            tolerance = 2.0 / 255.0 if channel == 3 else absolute
            if not math.isfinite(value) or not math.isclose(value, target, abs_tol=tolerance, rel_tol=relative if channel != 3 else 0.0):
                raise AssertionError(f"pixel {index}, channel {channel}: {value} != {target} (absolute {tolerance}, relative {relative})")


def srgb_to_linear(value: float) -> float:
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def normalized(pixels: tuple[tuple[int, ...], ...]) -> tuple[tuple[float, ...], ...]:
    return tuple(tuple(srgb_to_linear(value / 255.0) if channel < 3 else value / 255.0 for channel, value in enumerate(pixel)) for pixel in pixels)


def require_matching_payloads(actual: pathlib.Path, expected: pathlib.Path, description: str) -> None:
    if actual.read_bytes() != expected.read_bytes() or read_raw_pair(actual) != read_raw_pair(expected):
        raise AssertionError(f"{description} differs from equivalent RGBA input: {actual} != {expected}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tex-conv", required=True)
    parser.add_argument("--prepared-decode", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()
    output_dir = pathlib.Path(args.output_dir).resolve() / "prepared"
    output_dir.mkdir(parents=True, exist_ok=True)

    checker = tuple(((x * 37 + y * 11) & 255, (x * 17 + y * 53) & 255, (x * 71 + y * 29) & 255, 255 if (x + y) % 3 else 128) for y in range(5) for x in range(7))
    source_path = output_dir / "checker.png"
    write_png_pixels(source_path, 7, 5, 6, checker)
    prepared = run_conversion(args.tex_conv, output_dir, "checker", (source_path,))
    if read_raw_pair(prepared)["mips"] != [(7, 5, 1), (3, 2, 1), (1, 1, 1)]:
        raise AssertionError("non-power-of-two preparation changed its complete mip chain")
    overwrite = subprocess.run([args.tex_conv, str(source_path)], cwd=output_dir, text=True, capture_output=True)
    if overwrite.returncode != 1 or "Output already exists:" not in overwrite.stdout + overwrite.stderr or "[ERROR]:" not in overwrite.stdout + overwrite.stderr:
        raise AssertionError("existing output was not rejected as an error without --force")
    verify_publication_failure_preserves_outputs(args.tex_conv, source_path, prepared.with_suffix(".nwb"), prepared, output_dir)

    gray = output_dir / "gray.png"
    write_png_pixels(gray, 2, 2, 6, ((128, 128, 128, 128),) * 4)
    srgb = run_conversion(args.tex_conv, output_dir, "gray_srgb", (gray,))
    linear = run_conversion(args.tex_conv, output_dir, "gray_linear", (gray,), "--linear")
    require_pixels(decode_pixels(args.prepared_decode, srgb), ((srgb_to_linear(128.0 / 255.0),) * 3 + (128.0 / 255.0,),) * 4)
    require_pixels(decode_pixels(args.prepared_decode, linear), ((128.0 / 255.0,) * 4,) * 4)

    black_white = output_dir / "black_white.png"
    write_png_pixels(black_white, 2, 2, 6, ((0, 0, 0, 255), (255, 255, 255, 255), (255, 255, 255, 255), (0, 0, 0, 255)))
    black_white_prepared = run_conversion(args.tex_conv, output_dir, "black_white", (black_white,))
    require_pixels(decode_pixels(args.prepared_decode, black_white_prepared, 1), ((0.5, 0.5, 0.5, 1.0),))

    pulse_paths = []
    for index in range(5):
        path = output_dir / f"pulse_{index}.png"
        value = 255 if index == 2 else 0
        write_png_pixels(path, 1, 1, 6, ((value, value, value, 255),))
        pulse_paths.append(path)
    pulse = run_conversion(args.tex_conv, output_dir, "pulse", tuple(pulse_paths), "--volume", "--linear")
    for level, count in ((1, 2), (2, 1)):
        require_pixels(decode_pixels(args.prepared_decode, pulse, level), ((0.2, 0.2, 0.2, 1.0),) * count)

    # Equivalent RGBA references distinguish original alpha, mask red, constants, and absent input alpha.
    colors = tuple(((19 + x * 43 + y * 7) & 255, (71 + x * 11 + y * 59) & 255, (137 + x * 29 + y * 17) & 255) for y in range(4) for x in range(4))
    original_alpha = (255, 0, 71, 19, 168, 253, 3, 110, 47, 202, 88, 144, 30, 226, 62, 180)
    mask_alpha = (12, 46, 91, 153, 218, 33, 76, 129, 182, 241, 65, 112, 168, 205, 28, 187)
    alpha_source = output_dir / "alpha_input.png"
    write_png_pixels(alpha_source, 4, 4, 6, tuple((*rgb, alpha) for rgb, alpha in zip(colors, original_alpha)))
    original = run_conversion(args.tex_conv, output_dir, "alpha_original", (alpha_source,))
    for name, values, override in (("original_mask", original_alpha, "mask"), ("red_mask", mask_alpha, "mask"), ("white", (255,) * 16, "white"), ("black", (0,) * 16, "black")):
        reference_path = output_dir / f"{name}_reference.png"
        write_png_pixels(reference_path, 4, 4, 6, tuple((*rgb, alpha) for rgb, alpha in zip(colors, values)))
        reference = run_conversion(args.tex_conv, output_dir, f"{name}_reference", (reference_path,))
        argument = override
        if override == "mask":
            mask = output_dir / f"{name}.png"
            write_png_pixels(mask, 4, 4, 6, tuple((alpha, (alpha + 83) & 255, (alpha + 159) & 255, 255 - alpha) for alpha in values))
            argument = str(mask)
        actual = run_conversion(args.tex_conv, output_dir, f"alpha_{name}", (alpha_source,), "--alpha", argument)
        require_matching_payloads(actual, reference, f"alpha {name}")
        if name == "original_mask":
            require_matching_payloads(actual, original, "default original alpha")
    opaque_source = output_dir / "rgb_only.png"
    write_png_pixels(opaque_source, 4, 4, 2, colors)
    opaque = run_conversion(args.tex_conv, output_dir, "rgb_only", (opaque_source,))
    require_matching_payloads(opaque, output_dir / "white_reference.tex", "missing original alpha")

    # Uniform planes make cube face and volume slice ordering observable without depending on lossy edge fitting.
    for dimension, count, width, height in (("cube", 6, 2, 2), ("volume", 3, 4, 2)):
        inputs, expected = [], []
        for plane in range(count):
            path = output_dir / f"{dimension}_{plane}.png"
            pixel = (32 + plane * 31, 64 + plane * 17, 16 + plane * 23, 255)
            write_png_pixels(path, width, height, 6, (pixel,) * (width * height))
            inputs.append(path)
            expected.extend(normalized((pixel,) * (width * height)))
        result = run_conversion(args.tex_conv, output_dir, dimension, tuple(inputs), f"--{dimension}")
        if read_raw_pair(result)["dimension"] != (1 if dimension == "cube" else 2):
            raise AssertionError(f"wrong prepared {dimension} dimension")
        require_pixels(decode_pixels(args.prepared_decode, result), tuple(expected))

    radiance_path = output_dir / "radiance.hdr"
    radiance_pixels = ((4.5, 0.25, 1.5, 1.0),) * 8
    write_radiance_hdr_pixels(radiance_path, 4, 2, radiance_pixels)
    radiance = run_conversion(args.tex_conv, output_dir, "radiance", (radiance_path,))
    require_pixels(decode_pixels(args.prepared_decode, radiance), radiance_pixels, absolute=0.025, relative=0.05)
    if read_raw_pair(radiance)["alpha_mode"] != 0:
        raise AssertionError("opaque HDR alpha was not inferred")
    for mode, alpha in (("white", 1.0), ("black", 0.0)):
        result = run_conversion(args.tex_conv, output_dir, f"radiance_{mode}", (radiance_path,), "--alpha", mode)
        require_pixels(decode_pixels(args.prepared_decode, result), tuple((*pixel[:3], alpha) for pixel in radiance_pixels), absolute=0.025, relative=0.05)
        if mode == "white":
            require_matching_payloads(result, radiance, "HDR alpha white")
        elif read_raw_pair(result)["alpha_mode"] != 2 or read_raw_pair(result)["alpha_constant"] != 0:
            raise AssertionError("zero HDR alpha did not use its constant transport")

    for dimension, count in (("cube", 6), ("volume", 3)):
        inputs, expected = [], []
        for plane in range(count):
            path = output_dir / f"hdr_{dimension}_{plane}.hdr"
            pixel = (2.0 + plane, 0.25, 1.5, 1.0)
            write_radiance_hdr_pixels(path, 2, 2, (pixel,) * 4)
            inputs.append(path)
            expected.extend((pixel,) * 4)
        result = run_conversion(args.tex_conv, output_dir, f"hdr_{dimension}", tuple(inputs), f"--{dimension}")
        if read_raw_pair(result)["format"] != 1:
            raise AssertionError(f"HDR {dimension} lost its HDR payload")
        require_pixels(decode_pixels(args.prepared_decode, result), tuple(expected), absolute=0.025, relative=0.05)

    # Actual EXR channel declarations distinguish real equal-valued RGBA alpha from single-channel luminance.
    equal = tuple((value,) * 4 for value in (0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 0.25))
    exr_cases = (("equal_rgba_constant", ((0.5,) * 4,) * 8, ("A", "B", "G", "R"), 2), ("equal_rgba_varying", equal, ("A", "B", "G", "R"), 3), ("single_luminance", equal, ("Y",), 0))
    for name, pixels, channels, alpha_mode in exr_cases:
        path = output_dir / f"{name}.exr"
        write_openexr(path, pixels, channels)
        result = run_conversion(args.tex_conv, output_dir, name, (path,))
        if read_raw_pair(result)["alpha_mode"] != alpha_mode:
            raise AssertionError(f"EXR channel metadata changed alpha transport: {name}")
        expected = tuple((*pixel[:3], 1.0) for pixel in pixels) if len(channels) == 1 else pixels
        require_pixels(decode_pixels(args.prepared_decode, result), expected, absolute=0.04, relative=0.05)

    # UINT channels carry numeric integers, including alpha; mixed planes must use their own declared type.
    uint_exr_cases = (
        ("uint_rgba", (7, 3, 11, 1), (EXR_UINT,) * 4, 0),
        ("uint_rgb_float_alpha", (7, 0.25, 11, 0.5), (EXR_FLOAT, EXR_UINT, EXR_FLOAT, EXR_UINT), 2),
        ("float_rgb_uint_alpha", (2.0, 0.25, 1.5, 1), (EXR_UINT, EXR_FLOAT, EXR_FLOAT, EXR_FLOAT), 0),
    )
    for name, pixel, pixel_types, alpha_mode in uint_exr_cases:
        path = output_dir / f"{name}.exr"
        pixels = (pixel,) * 8
        write_openexr(path, pixels, channel_types=pixel_types)
        result = run_conversion(args.tex_conv, output_dir, name, (path,))
        description = read_raw_pair(result)
        if description["format"] != 1 or description["alpha_mode"] != alpha_mode:
            raise AssertionError(f"EXR integer channel types changed HDR or alpha transport: {name}")
        require_pixels(decode_pixels(args.prepared_decode, result), pixels, absolute=0.04, relative=0.05)

    # Invalid float pixels must fail before forced publication replaces either accepted output.
    original_pair = (prepared.with_suffix(".nwb").read_bytes(), prepared.read_bytes())
    invalid_path = output_dir / "invalid.exr"
    valid_pixels = ((9.0, 0.5, 2.0, 0.375),) * 8
    for channel, value in ((0, math.nan), (1, math.inf), (2, -0.125), (0, 65217.0), (3, math.nan), (3, math.inf), (3, -0.125), (3, 1.125)):
        first = list(valid_pixels[0])
        first[channel] = value
        write_openexr(invalid_path, (tuple(first), *valid_pixels[1:]))
        invalid = subprocess.run([args.tex_conv, str(invalid_path), "--force", "--output", str(prepared.with_suffix(".nwb"))], cwd=output_dir, text=True, capture_output=True)
        if invalid.returncode != 1:
            raise AssertionError(f"invalid float channel {channel}: {value} was accepted\n{invalid.stdout}\n{invalid.stderr}")
        if (prepared.with_suffix(".nwb").read_bytes(), prepared.read_bytes()) != original_pair:
            raise AssertionError("invalid source changed an accepted prepared texture pair")
        if any(pathlib.Path(str(path) + suffix).exists() for path in (prepared.with_suffix(".nwb"), prepared) for suffix in (".tmp", ".old")):
            raise AssertionError("invalid source retained publication work files")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exception:
        print(f"tex_conv test failed: {exception}", file=sys.stderr)
        raise SystemExit(1)
