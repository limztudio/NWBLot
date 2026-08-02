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
    cube_metadata_path = output_dir / "cube.nwb"
    cube_texture_path = output_dir / "cube.tex"
    volume_metadata_path = output_dir / "volume.nwb"
    volume_texture_path = output_dir / "volume.tex"
    cube_face_paths = [output_dir / f"cube_face_{face}.png" for face in range(6)]
    volume_slice_paths = [output_dir / f"volume_slice_{slice_index}.png" for slice_index in range(3)]
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
        pathlib.Path(f"{metadata_path}.tmp"),
        pathlib.Path(f"{texture_path}.tmp"),
        pathlib.Path(f"{linear_metadata_path}.tmp"),
        pathlib.Path(f"{linear_texture_path}.tmp"),
        pathlib.Path(f"{cube_metadata_path}.tmp"),
        pathlib.Path(f"{cube_texture_path}.tmp"),
        pathlib.Path(f"{volume_metadata_path}.tmp"),
        pathlib.Path(f"{volume_texture_path}.tmp"),
        *cube_face_paths,
        *volume_slice_paths,
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

    # Faces are supplied in the on-disk/Basis convention: +X, -X, +Y, -Y, +Z, -Z.
    for face_index, face_path in enumerate(cube_face_paths):
        write_rgba_png(face_path, 2, 2, (face_index * 31, face_index * 17, face_index * 47, 255))
    cube = subprocess.run(
        [
            args.tex_conv,
            "--cube",
            *(str(face_path) for face_path in cube_face_paths),
            "--output",
            str(output_dir / "cube"),
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
    cube_metadata = cube_metadata_path.read_text(encoding="utf-8")
    for fragment in (
        "asset.version = 2;",
        'asset.dimension = "cube";',
        "asset.depth = 1;",
        "asset.width = 2;",
        "asset.height = 2;",
        'asset.payload_layout = "mip_major_slice_major_blocks";',
        "asset.mip_count = 2;",
        '"level": 0, "width": 2, "height": 2',
        '"level": 1, "width": 1, "height": 1',
        '"slices": 6',
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
            "--volume",
            *(str(slice_path) for slice_path in volume_slice_paths),
            "--output",
            str(output_dir / "volume"),
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
    volume_metadata = volume_metadata_path.read_text(encoding="utf-8")
    for fragment in (
        "asset.version = 2;",
        'asset.dimension = "volume";',
        "asset.depth = 3;",
        "asset.width = 4;",
        "asset.height = 2;",
        'asset.payload_layout = "mip_major_slice_major_blocks";',
        "asset.mip_count = 3;",
        '"level": 0, "width": 4, "height": 2',
        '"level": 1, "width": 2, "height": 1',
        '"level": 2, "width": 1, "height": 1',
        '"slices": 3',
        'asset.data = "volume.tex";',
    ):
        require_metadata_fragment(volume_metadata, fragment)
    if volume_texture_path.stat().st_size != 80:
        raise AssertionError(f"expected 80 volume UASTC bytes, got {volume_texture_path.stat().st_size}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exception:
        print(f"tex_conv test failed: {exception}", file=sys.stderr)
        raise SystemExit(1)
