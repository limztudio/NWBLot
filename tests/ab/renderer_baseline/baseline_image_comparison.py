from __future__ import annotations

import argparse
from pathlib import Path
from typing import List, Tuple

from window_capture_image_codec import Bmp24Codec, PixelDiff

PixelDifference = PixelDiff


class BaselineImageComparison:
    @staticmethod
    def read_bmp_rgb(path: Path) -> Tuple[int, int, bytes]:
        return Bmp24Codec.read_bmp_24_rgb(path, allow_extra_bytes=True)

    @staticmethod
    def write_bmp_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
        Bmp24Codec.write_bmp_24_rgb(path, width, height, rgb)

    @staticmethod
    def compare_bmp_rgb(reference_path: Path, candidate_path: Path, difference_path: Path) -> PixelDifference:
        reference_width, reference_height, reference_rgb = BaselineImageComparison.read_bmp_rgb(reference_path)
        candidate_width, candidate_height, candidate_rgb = BaselineImageComparison.read_bmp_rgb(candidate_path)
        return Bmp24Codec.compare_bmp_24_rgb(reference_width, reference_height, reference_rgb, candidate_width, candidate_height, candidate_rgb,
            difference_path=difference_path, dimension_error="reference and candidate captures have different dimensions: {0}x{1} vs {2}x{3}")

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
