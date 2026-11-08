"""Read capture bitmaps and compare their pixels for the async-shadow benchmark."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List

from window_capture_smoke import SmokeFailure


@dataclass(frozen=True)
class PixelDiff:
    width: int
    height: int
    max_abs: int
    mean_abs: float
    changed_pixels: int
    total_pixels: int

    @property
    def changed_fraction(self) -> float:
        return self.changed_pixels / self.total_pixels if self.total_pixels else 0.0


class BitmapImage:
    def __init__(self, path: Path):
        """Read the 24-bit BMPs written by the shared native-window capture helper."""
        data = path.read_bytes()
        if len(data) < 54 or data[:2] != b"BM":
            raise SmokeFailure(f"capture is not a BMP: {path}")

        pixel_offset = struct.unpack_from("<I", data, 10)[0]
        dib_size = struct.unpack_from("<I", data, 14)[0]
        if dib_size < 40 or len(data) < 14 + dib_size:
            raise SmokeFailure(f"capture has an unsupported DIB header: {path}")
        if pixel_offset < 14 + dib_size:
            raise SmokeFailure(f"capture pixel data overlaps its headers: {path}")

        width, signed_height, planes, bits_per_pixel, compression = struct.unpack_from("<iiHHI", data, 18)
        if width <= 0 or signed_height == 0 or planes != 1 or bits_per_pixel != 24 or compression != 0:
            raise SmokeFailure(f"capture must be an uncompressed 24-bit BMP: {path}")

        height = abs(signed_height)
        source_stride = ((width * 3 + 3) // 4) * 4
        required_bytes = pixel_offset + source_stride * height
        if required_bytes > len(data):
            raise SmokeFailure(f"capture pixel data is truncated: {path}")

        rows: List[bytes] = []
        bottom_up = signed_height > 0
        for logical_row in range(height):
            source_row = height - 1 - logical_row if bottom_up else logical_row
            row_offset = pixel_offset + source_row * source_stride
            source = data[row_offset:row_offset + width * 3]
            rgb = bytearray(width * 3)
            for pixel in range(width):
                source_offset = pixel * 3
                rgb[source_offset] = source[source_offset + 2]
                rgb[source_offset + 1] = source[source_offset + 1]
                rgb[source_offset + 2] = source[source_offset]
            rows.append(bytes(rgb))
        self.width = width
        self.height = height
        self.rgb = b"".join(rows)

    def compare(self, other: BitmapImage) -> PixelDiff:
        first_width, first_height, first_rgb = self.width, self.height, self.rgb
        second_width, second_height, second_rgb = other.width, other.height, other.rgb
        if (first_width, first_height) != (second_width, second_height):
            raise SmokeFailure(f"pixel-parity captures have different dimensions: {first_width}x{first_height} vs " f"{second_width}x{second_height}")

        total_abs = 0
        max_abs = 0
        changed_pixels = 0
        for pixel_offset in range(0, len(first_rgb), 3):
            pixel_changed = False
            for channel in range(3):
                delta = abs(first_rgb[pixel_offset + channel] - second_rgb[pixel_offset + channel])
                total_abs += delta
                max_abs = max(max_abs, delta)
                pixel_changed = pixel_changed or delta != 0
            if pixel_changed:
                changed_pixels += 1

        total_pixels = first_width * first_height
        return PixelDiff(width=first_width, height=first_height, max_abs=max_abs, mean_abs=total_abs / len(first_rgb), changed_pixels=changed_pixels, total_pixels=total_pixels,)
