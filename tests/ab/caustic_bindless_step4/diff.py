#!/usr/bin/env python3
"""Pixel-parity diff for caustic captures.

Photon Monte Carlo and temporal reuse leave diffuse noise-floor grain in the caustic
footprint. A structured hard-edged band indicates a non-uniform-indexing failure at a
wave-tile boundary.

    python diff.py <before.(bmp|png)> <after.(bmp|png)> <diff-out.png>
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pixel_diff import main

if __name__ == "__main__":
    sys.exit(main(sys.argv))
