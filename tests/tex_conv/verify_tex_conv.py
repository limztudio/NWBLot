#!/usr/bin/env python3

import argparse
import pathlib
import struct
import subprocess
import sys
import zlib


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


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

    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(bytes(rows)))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def require_metadata_fragment(metadata: str, fragment: str) -> None:
    if fragment not in metadata:
        raise AssertionError(f"metadata does not contain: {fragment!r}\n{metadata}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tex-conv", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    output_dir = pathlib.Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    source_path = output_dir / "checker.png"
    metadata_path = output_dir / "checker.nwb"
    texture_path = output_dir / "checker.tex"
    linear_metadata_path = output_dir / "linear.nwb"
    linear_texture_path = output_dir / "linear.tex"
    for path in (
        source_path,
        metadata_path,
        texture_path,
        linear_metadata_path,
        linear_texture_path,
        pathlib.Path(f"{metadata_path}.tmp"),
        pathlib.Path(f"{texture_path}.tmp"),
        pathlib.Path(f"{linear_metadata_path}.tmp"),
        pathlib.Path(f"{linear_texture_path}.tmp"),
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

    metadata = metadata_path.read_text(encoding="utf-8")
    for fragment in (
        "texture asset;",
        'asset.format = "uastc_ldr_4x4";',
        'asset.uastc_spec_revision = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d";',
        'asset.color_space = "srgb";',
        "asset.width = 7;",
        "asset.height = 5;",
        "asset.block_width = 4;",
        "asset.block_height = 4;",
        "asset.bytes_per_block = 16;",
        'asset.payload_layout = "mip_major_blocks";',
        'asset.mip_address_mode = "clamp";',
        "asset.has_alpha = 1;",
        "asset.mip_count = 3;",
        'asset.data = "checker.tex";',
        '{ "level": 0, "width": 7, "height": 5, "blocks_x": 2, "blocks_y": 2, "offset_bytes": 0, "size_bytes": 64 }',
        '{ "level": 1, "width": 3, "height": 2, "blocks_x": 1, "blocks_y": 1, "offset_bytes": 64, "size_bytes": 16 }',
        '{ "level": 2, "width": 1, "height": 1, "blocks_x": 1, "blocks_y": 1, "offset_bytes": 80, "size_bytes": 16 }',
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
    if overwrite.returncode == 0 or "Output already exists:" not in overwrite_output:
        raise AssertionError("tex_conv did not protect an existing output without --force")

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
        [args.tex_conv, str(source_path), "--output", str(output_dir / "linear"), "--linear"],
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
        linear_metadata_path.read_text(encoding="utf-8"),
        'asset.color_space = "linear";',
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exception:
        print(f"tex_conv test failed: {exception}", file=sys.stderr)
        raise SystemExit(1)
