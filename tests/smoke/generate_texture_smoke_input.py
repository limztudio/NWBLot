#!/usr/bin/env python3
"""Generate the deterministic source image for the texture runtime smoke."""

import argparse
from pathlib import Path
import sys
from typing import Tuple


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))

from png_fixture import write_png_rows  # noqa: E402


def texture_pixel(x: int, y: int) -> Tuple[int, int, int, int]:
    # Make red the dominant reflected colour while retaining distinct green and blue texels.  A balanced checker
    # averages to near-grey over a diffuse sphere, which makes a perfectly valid colored indirect bounce invisible on
    # the white receiver.  This intentionally biased source is therefore a better end-to-end texture + GI fixture:
    # its receiver-side spill must be red, while the minority green/blue texels still prove that real UV sampling is
    # taking place rather than a constant-color fallback.
    palette = (
        (244, 42, 42),
        (244, 42, 42),
        (244, 42, 42),
        (244, 42, 42),
        (240, 84, 42),
        (240, 84, 42),
        (52, 218, 104),
        (56, 112, 246),
    )
    tile_size = 8
    if x % tile_size == 0 or y % tile_size == 0:
        return (22, 24, 30, 255)

    color = palette[((x // tile_size) + 2 * (y // tile_size)) % len(palette)]
    highlight = 16 if ((x + y) & 4) else 0
    return tuple(min(channel + highlight, 255) for channel in color) + (255,)


def write_texture_png(path: Path) -> None:
    width = 64
    height = 64
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(texture_pixel(x, y))

    path.parent.mkdir(parents=True, exist_ok=True)
    write_png_rows(path, width, height, 6, rows, compression_level=9)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    write_texture_png(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
