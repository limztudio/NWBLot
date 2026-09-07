"""Encode deterministic, unfiltered PNG rows for generated test fixtures."""

from __future__ import annotations

from pathlib import Path
import struct
import zlib


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def write_png_rows(
    path: Path,
    width: int,
    height: int,
    color_type: int,
    rows: bytes | bytearray,
    *,
    compression_level: int = -1,
) -> None:
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), level=compression_level))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(png)
