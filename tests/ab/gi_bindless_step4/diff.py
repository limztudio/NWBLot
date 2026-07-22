#!/usr/bin/env python3
"""Pixel-parity diff for the GI bindless step-4b A/B.

The migration is a pure descriptor-indirection swap (heap slot -> the same
underlying buffer the bounded per-mesh array held), so the ONLY expected difference is the
surfel-GI producer's temporal floor (DDGI probe Monte-Carlo trace + temporal-blend/EMA),
which concentrates in the bounced-lighting features (the red/blue indirect bleed on the
shadowed floor). A correct 4b diff is therefore indistinguishable from a two-identical-run
noise-floor diff -- diffuse grain in the GI features. A structured, hard-edged band (a
non-uniform-indexing miss reading another mesh's geometry along a wave-tile boundary) is
the failure signature and would push the max FAR above the floor.

    python diff.py <before.(bmp|png)> <after.(bmp|png)> <diff-out.png>
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pixel_diff import main

if __name__ == "__main__":
    sys.exit(main(sys.argv))
