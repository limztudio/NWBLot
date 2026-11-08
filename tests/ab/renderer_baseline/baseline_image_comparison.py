"""Read and compare the renderer baseline capture images."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple

from window_capture_smoke import SmokeFailure

LIT_B_BM = b"BM"
LIT_I = "<I"


@dataclass(frozen=True)
class PixelDifference:
    width: int
    height: int
    max_abs: int
    mean_abs: float
    changed_pixels: int
    total_pixels: int

    @property
    def changed_fraction(self) -> float:
        return self.changed_pixels / self.total_pixels if self.total_pixels else 0.0


class BaselineImageComparison:
    @staticmethod
    def read_bmp_rgb(path: Path) -> Tuple[int, int, bytes]:
        try:
            data = path.read_bytes()
        except OSError as error:
            raise SmokeFailure(f"could not read BMP '{path}': {error}") from error
        if len(data) < 54 or data[:2] != LIT_B_BM:
            raise SmokeFailure(f"capture is not a BMP: {path}")

        pixel_offset = struct.unpack_from(LIT_I, data, 10)[0]
        dib_size = struct.unpack_from(LIT_I, data, 14)[0]
        if dib_size < 40 or len(data) < 14 + dib_size:
            raise SmokeFailure(f"capture has an unsupported DIB header: {path}")

        width, signed_height, planes, bits_per_pixel, compression = struct.unpack_from("<iiHHI", data, 18)
        if width <= 0 or signed_height == 0 or planes != 1 or bits_per_pixel != 24 or compression != 0:
            raise SmokeFailure(f"capture must be an uncompressed 24-bit BMP: {path}")

        height = abs(signed_height)
        source_stride = ((width * 3 + 3) // 4) * 4
        if pixel_offset + source_stride * height > len(data):
            raise SmokeFailure(f"capture pixel data is truncated: {path}")

        rows: List[bytes] = []
        bottom_up = signed_height > 0
        for logical_row in range(height):
            source_row = height - 1 - logical_row if bottom_up else logical_row
            source_offset = pixel_offset + source_row * source_stride
            bgr = data[source_offset:source_offset + width * 3]
            rgb = bytearray(width * 3)
            for pixel in range(0, len(bgr), 3):
                rgb[pixel] = bgr[pixel + 2]
                rgb[pixel + 1] = bgr[pixel + 1]
                rgb[pixel + 2] = bgr[pixel]
            rows.append(bytes(rgb))
        return width, height, b"".join(rows)


    @staticmethod
    def write_bmp_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
        if width <= 0 or height <= 0 or len(rgb) != width * height * 3:
            raise ValueError("invalid RGB baseline image dimensions or byte count")

        path.parent.mkdir(parents=True, exist_ok=True)
        row_stride = ((width * 3 + 3) // 4) * 4
        image_size = row_stride * height
        padding = b"\0" * (row_stride - width * 3)
        with path.open("wb") as output:
            output.write(struct.pack("<2sIHHI", LIT_B_BM, 14 + 40 + image_size, 0, 0, 54))
            output.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
            for row_index in range(height - 1, -1, -1):
                row = rgb[row_index * width * 3:(row_index + 1) * width * 3]
                bgr = bytearray(width * 3)
                for pixel in range(0, len(row), 3):
                    bgr[pixel] = row[pixel + 2]
                    bgr[pixel + 1] = row[pixel + 1]
                    bgr[pixel + 2] = row[pixel]
                output.write(bgr)
                output.write(padding)


    @staticmethod
    def compare_bmp_rgb(reference_path: Path, candidate_path: Path, difference_path: Path) -> PixelDifference:
        reference_width, reference_height, reference_rgb = BaselineImageComparison.read_bmp_rgb(reference_path)
        candidate_width, candidate_height, candidate_rgb = BaselineImageComparison.read_bmp_rgb(candidate_path)
        if (reference_width, reference_height) != (candidate_width, candidate_height):
            raise SmokeFailure("reference and candidate captures have different dimensions: " f"{reference_width}x{reference_height} vs {candidate_width}x{candidate_height}")

        total_abs = 0
        max_abs = 0
        changed_pixels = 0
        difference_rgb = bytearray(len(reference_rgb))
        for offset in range(0, len(reference_rgb), 3):
            changed = False
            for channel in range(3):
                delta = abs(reference_rgb[offset + channel] - candidate_rgb[offset + channel])
                total_abs += delta
                max_abs = max(max_abs, delta)
                difference_rgb[offset + channel] = min(255, delta * 8)
                changed = changed or delta != 0
            if changed:
                changed_pixels += 1

        BaselineImageComparison.write_bmp_rgb(difference_path, reference_width, reference_height, bytes(difference_rgb))
        total_pixels = reference_width * reference_height
        return PixelDifference(width=reference_width, height=reference_height, max_abs=max_abs, mean_abs=total_abs / len(reference_rgb), changed_pixels=changed_pixels, total_pixels=total_pixels,)


    @staticmethod
    def difference_failures(args: argparse.Namespace, difference: PixelDifference) -> List[str]:
        maximum_max_abs = 0 if args.require_exact else args.maximum_max_abs
        maximum_mean_abs = 0.0 if args.require_exact else args.maximum_mean_abs
        maximum_changed_fraction = 0.0 if args.require_exact else args.maximum_changed_fraction
        failures: List[str] = []
        if maximum_max_abs is not None and difference.max_abs > maximum_max_abs:
            failures.append(f"max abs {difference.max_abs} exceeds {maximum_max_abs}")
        if maximum_mean_abs is not None and difference.mean_abs > maximum_mean_abs:
            failures.append(f"mean abs {difference.mean_abs:.6f} exceeds {maximum_mean_abs:.6f}")
        if maximum_changed_fraction is not None and difference.changed_fraction > maximum_changed_fraction:
            failures.append(f"changed fraction {difference.changed_fraction:.6f} exceeds {maximum_changed_fraction:.6f}")
        return failures

