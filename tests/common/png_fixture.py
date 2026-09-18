"""Encode deterministic, unfiltered PNG rows for generated test fixtures."""

from __future__ import annotations

from pathlib import Path
import struct
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_IHDR_CHUNK = b"IHDR"
PNG_IDAT_CHUNK = b"IDAT"
PNG_IEND_CHUNK = b"IEND"
PNG_CRC_MASK = 0xFFFFFFFF
PNG_IHDR_BIT_DEPTH = 8
PNG_IHDR_COMPRESSION_FILTER_INTERLACE = (0, 0, 0)
PNG_BIG_ENDIAN_U32 = ">I"
PNG_IHDR_STRUCT = ">IIBBBBB"
PNG_DEFAULT_COMPRESSION_LEVEL = -1


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(PNG_BIG_ENDIAN_U32, len(data))
        + kind
        + data
        + struct.pack(PNG_BIG_ENDIAN_U32, zlib.crc32(kind + data) & PNG_CRC_MASK)
    )


def write_png_rows(
    path: Path,
    width: int,
    height: int,
    color_type: int,
    rows: bytes | bytearray,
    *,
    compression_level: int = PNG_DEFAULT_COMPRESSION_LEVEL,
) -> None:
    png = (
        PNG_SIGNATURE
        + png_chunk(PNG_IHDR_CHUNK, struct.pack(PNG_IHDR_STRUCT, width, height, PNG_IHDR_BIT_DEPTH, color_type, *PNG_IHDR_COMPRESSION_FILTER_INTERLACE))
        + png_chunk(PNG_IDAT_CHUNK, zlib.compress(bytes(rows), level=compression_level))
        + png_chunk(PNG_IEND_CHUNK, b"")
    )
    path.write_bytes(png)
