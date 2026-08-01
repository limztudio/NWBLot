#!/usr/bin/env python3
"""Pixel-parity diff for deterministic soft-shadow captures.

The frozen caster yaw makes a nonzero max difference a real divergence; a structured bright
band points to a non-uniform-indexing failure at a wave-tile boundary.

    python diff.py <before.(bmp|png)> <after.(bmp|png)> <diff-out.png>
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pixel_diff import main

if __name__ == "__main__":
    sys.exit(main(sys.argv))
