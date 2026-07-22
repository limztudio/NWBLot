#!/usr/bin/env python3
"""Pixel-parity diff for the caustic bindless step-4b A/B.

The migration is a pure descriptor-indirection swap (heap slot -> the same
underlying buffer the bounded array held), so the ONLY expected difference is the caustic
producer's temporal-jitter floor (photon Monte-Carlo + 2x temporal-reuse EMA), which
concentrates in the caustic footprint. A correct 4b diff is therefore indistinguishable
from a two-identical-run noise-floor diff -- diffuse grain in the caustic/lighting
features. A structured, hard-edged band (a non-uniform-indexing miss reading another mesh's
geometry along a wave-tile boundary) is the failure signature and would push the max FAR
above the floor.

    python diff.py <before.(bmp|png)> <after.(bmp|png)> <diff-out.png>
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pixel_diff import main

if __name__ == "__main__":
    sys.exit(main(sys.argv))
