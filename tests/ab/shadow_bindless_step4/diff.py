#!/usr/bin/env python3
"""Pixel-parity diff for the SW-shadow bindless step-4b A/B.

The migration is a pure descriptor-indirection swap (heap slot -> same
underlying buffer as the bounded array), so a correct 4b is EXACTLY zero difference; any
nonzero max flags a real divergence (a non-uniform-indexing miss reads a different mesh's
geometry along a wave-tile boundary -> a bright band).

    python diff.py <before.(bmp|png)> <after.(bmp|png)> <diff-out.png>
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pixel_diff import main

if __name__ == "__main__":
    sys.exit(main(sys.argv))
